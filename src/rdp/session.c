/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <winsock.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "net.h"
#include "frame.h"
#include "session.h"
#include "rdp.h"

#define FLUSH_MAX_MS   5000
#define CLOSE_DRAIN_MS 2000
#define WAKE_BAD_MAX   50

void session_end(SESSION *s, int flush, const char *fmt, ...)
{
    va_list ap;

    if (s->ending != END_NO) {
        if (!flush) {
            s->ending = END_NOW;
        }
        return;
    }
    va_start(ap, fmt);
    _vsnprintf(s->why, sizeof(s->why) - 1, fmt, ap);
    va_end(ap);
    s->why[sizeof(s->why) - 1] = '\0';
    s->ending = flush ? END_FLUSH : END_NOW;
    s->end_ms = GetTickCount();
}

static void wake_open(SESSION *s)
{
    int len = sizeof(s->wake_to);
    u_long on = 1;
    int err;

    s->wake = socket(AF_INET, SOCK_DGRAM, 0);
    if (s->wake == INVALID_SOCKET) {
        err = WSAGetLastError();
    } else {
        memset(&s->wake_to, 0, sizeof(s->wake_to));
        s->wake_to.sin_family = AF_INET;
        s->wake_to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(s->wake, (struct sockaddr *) &s->wake_to,
                 sizeof(s->wake_to)) != SOCKET_ERROR &&
            getsockname(s->wake, (struct sockaddr *) &s->wake_to,
                        &len) != SOCKET_ERROR &&
            ioctlsocket(s->wake, FIONBIO, &on) != SOCKET_ERROR) {
            s->wake_to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            return;
        }
        err = WSAGetLastError();
        closesocket(s->wake);
        s->wake = INVALID_SOCKET;
    }
    log_msg(LOG_WARN, "session %s: no wake-up socket on 127.0.0.1, error "
            "%d%s: screen changes can wait up to %d ms", s->peer, err,
            net_error_text(err), NET_POLL_MS);
}

void session_wake(SESSION *s)
{
    char b = 0;

    if (s->wake != INVALID_SOCKET && !s->wake_off &&
        InterlockedExchange((LONG *) &s->wake_due, 1) == 0) {
        sendto(s->wake, &b, 1, 0, (struct sockaddr *) &s->wake_to,
               sizeof(s->wake_to));
    }
}

static void flush(SESSION *s)
{
    int n, err;

    while (s->tx_pos < s->tx_len) {
        n = send(s->sock, (const char *) s->tx + s->tx_pos,
                 (int) (s->tx_len - s->tx_pos), 0);
        if (n == SOCKET_ERROR) {
            err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                session_end(s, 0, "send failed, error %d%s", err,
                            net_error_text(err));
            }
            return;
        }
        s->tx_pos += (unsigned int) n;
        s->tx_bytes += (unsigned long) n;
    }
    s->tx_pos = 0;
    s->tx_len = 0;
}

int session_send(SESSION *s, const unsigned char *pkt, unsigned int len)
{
    if (s->ending == END_NOW) {
        return 0;
    }
    if (s->tx_len + len > SESS_TX_MAX && s->tx_pos > 0) {
        memmove(s->tx, s->tx + s->tx_pos, s->tx_len - s->tx_pos);
        s->tx_len -= s->tx_pos;
        s->tx_pos = 0;
    }
    if (s->tx_len + len > SESS_TX_MAX) {
        session_end(s, 0, "send queue full (%u bytes queued, %u more)",
                    s->tx_len, len);
        return 0;
    }
    log_hex("out", pkt, len);
    memcpy(s->tx + s->tx_len, pkt, len);
    s->tx_len += len;
    s->tx_packets++;
    flush(s);
    return 1;
}

static void receive(SESSION *s)
{
    unsigned int pos, len;
    int n, kind, err;

    n = recv(s->sock, (char *) s->rx + s->rx_len,
             (int) (SESS_RX_MAX - s->rx_len), 0);
    if (n == 0) {
        if (s->rx_len > 0) {
            session_end(s, 0, "the client closed the connection in the "
                        "middle of a packet (%u bytes of it)", s->rx_len);
        } else {
            session_end(s, 0, "the client closed the connection");
        }
        return;
    }
    if (n == SOCKET_ERROR) {
        err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            session_end(s, 0, "recv failed, error %d%s", err,
                        net_error_text(err));
        }
        return;
    }
    s->rx_len += (unsigned int) n;
    s->rx_bytes += (unsigned long) n;

    pos = 0;
    while (s->ending == END_NO) {
        kind = frame_length(s->rx + pos, s->rx_len - pos, &len);
        if (kind == FRAME_ERROR) {
            log_hex("in, not a packet", s->rx + pos,
                    s->rx_len - pos < 64 ? s->rx_len - pos : 64);
            session_end(s, 0, "protocol error: the bytes are no TPKT and no "
                        "fast-path packet (first byte 0x%02x; rdp-1 2.1)",
                        (unsigned) s->rx[pos]);
            break;
        }
        if (kind == FRAME_MORE || len > s->rx_len - pos) {
            break;
        }
        s->rx_packets++;
        rdp_packet(s, kind, s->rx + pos, len);
        pos += len;
    }
    if (pos > 0) {
        memmove(s->rx, s->rx + pos, s->rx_len - pos);
        s->rx_len -= pos;
    }
}

static void close_connection(SESSION *s)
{
    char junk[512];
    fd_set rd;
    struct timeval tv;
    DWORD t0 = GetTickCount();
    int n;

    shutdown(s->sock, 1);
    while (GetTickCount() - t0 < CLOSE_DRAIN_MS) {
        FD_ZERO(&rd);
        FD_SET(s->sock, &rd);
        tv.tv_sec = 0;
        tv.tv_usec = NET_POLL_MS * 1000L;
        if (select(0, &rd, NULL, NULL, &tv) == SOCKET_ERROR) {
            break;
        }
        if (FD_ISSET(s->sock, &rd)) {
            n = recv(s->sock, junk, sizeof(junk), 0);
            if (n == 0 ||
                (n == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)) {
                break;
            }
        }
    }
    closesocket(s->sock);
}

static int wake_read(SESSION *s, char *buf, int len)
{
    struct sockaddr_in from;
    int from_len = sizeof(from);

    return recvfrom(s->wake, buf, len, 0, (struct sockaddr *) &from,
                    &from_len);
}

static void turn(SESSION *s)
{
    char junk[16];
    fd_set rd, wr;
    struct timeval tv;
    int err, n;

    FD_ZERO(&rd);
    FD_ZERO(&wr);
    if (s->ending == END_NO) {
        FD_SET(s->sock, &rd);
    }
    if (s->wake != INVALID_SOCKET && !s->wake_off) {
        FD_SET(s->wake, &rd);
    }
    if (s->tx_pos < s->tx_len) {
        FD_SET(s->sock, &wr);
    }
    tv.tv_sec = 0;
    tv.tv_usec = NET_POLL_MS * 1000L;
    n = select(0, &rd, &wr, NULL, &tv);
    if (n == SOCKET_ERROR) {
        err = WSAGetLastError();
        session_end(s, 0, "select failed, error %d%s", err,
                    net_error_text(err));
        return;
    }
    s->turns++;
    if (n == 0) {
        s->turns_none++;
    }
    if (GetTickCount() - s->turns_ms >= 10000) {
        log_msg(LOG_DEBUG, "session %s: loop: %lu turns in 10 s (%lu "
                "found nothing), wake-up socket readable %lu times (%lu "
                "with no byte)", s->peer, s->turns, s->turns_none,
                s->wake_reads, s->wake_errors);
        s->turns = s->turns_none = s->wake_reads = s->wake_errors = 0;
        s->turns_ms = GetTickCount();
    }
    if (FD_ISSET(s->sock, &wr)) {
        flush(s);
    }
    if (FD_ISSET(s->sock, &rd)) {
        receive(s);
    }
    if (s->wake != INVALID_SOCKET && !s->wake_off &&
        FD_ISSET(s->wake, &rd)) {
        s->wake_reads++;
        n = wake_read(s, junk, sizeof(junk));
        if (n > 0) {
            s->wake_bad = 0;
            while (wake_read(s, junk, sizeof(junk)) > 0) {
            }
        } else {
            err = n == 0 ? 0 : WSAGetLastError();
            s->wake_errors++;
            if (++s->wake_bad >= WAKE_BAD_MAX) {
                s->wake_off = 1;
                log_msg(LOG_WARN, "session %s: the wake-up socket does not "
                        "work here: select() says it can be read, but "
                        "recv() gets no byte (error %d%s, %d times in a "
                        "row). It is not used any more: a screen change "
                        "can wait up to %d ms", s->peer, err,
                        net_error_text(err), WAKE_BAD_MAX, NET_POLL_MS);
            }
        }
    }
    InterlockedExchange((LONG *) &s->wake_due, 0);
    rdp_pump(s);
}

void session_run(SOCKET sock, const char *peer, volatile LONG *stop)
{
    SESSION *s;
    u_long on = 1;
    int nodelay = 1, keepalive = 1;

    s = (SESSION *) calloc(1, sizeof(SESSION));
    if (s == NULL) {
        log_msg(LOG_ERROR, "session %s: out of memory", peer);
        closesocket(sock);
        return;
    }
    s->sock = sock;
    strncpy(s->peer, peer, sizeof(s->peer) - 1);
    s->stop = stop;
    s->start_ms = GetTickCount();
    wake_open(s);
    rdp_init(s);
    log_msg(LOG_INFO, "session %s: start", s->peer);

    if (ioctlsocket(sock, FIONBIO, &on) == SOCKET_ERROR) {
        session_end(s, 0, "ioctlsocket FIONBIO failed, error %d",
                    WSAGetLastError());
    }
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *) &nodelay,
                   sizeof(nodelay)) == SOCKET_ERROR) {
        log_msg(LOG_WARN, "session %s: TCP_NODELAY failed, error %d",
                s->peer, WSAGetLastError());
    }
    if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE,
                   (const char *) &keepalive,
                   sizeof(keepalive)) == SOCKET_ERROR) {
        log_msg(LOG_WARN, "session %s: SO_KEEPALIVE failed, error %d",
                s->peer, WSAGetLastError());
    }

    while (s->ending != END_NOW) {
        if (*s->stop != 0 && s->ending == END_NO) {
            rdp_service_stop(s);
        }
        rdp_check_time(s, GetTickCount());
        if (s->ending == END_FLUSH) {
            if (s->tx_pos == s->tx_len) {
                break;
            }
            if (GetTickCount() - s->end_ms > FLUSH_MAX_MS) {
                log_msg(LOG_WARN, "session %s: %u bytes not sent within "
                        "%d s", s->peer, s->tx_len - s->tx_pos,
                        FLUSH_MAX_MS / 1000);
                break;
            }
        }
        turn(s);
    }

    close_connection(s);
    log_msg(LOG_INFO, "session %s: closed: %s. In: %lu bytes, %lu packet%s"
            " (%lu fast-path, %lu input events). Out: %lu bytes, %lu packet%s."
            " %lu s, state %s.",
            s->peer, s->why, s->rx_bytes, s->rx_packets,
            s->rx_packets == 1 ? "" : "s", s->fastpath, s->in_events,
            s->tx_bytes, s->tx_packets, s->tx_packets == 1 ? "" : "s",
            (unsigned long) (GetTickCount() - s->start_ms) / 1000,
            rdp_state_name(s->state));
    rdp_done(s);
    if (s->wake != INVALID_SOCKET) {
        closesocket(s->wake);
    }
    free(s);
}
