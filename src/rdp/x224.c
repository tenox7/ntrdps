/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <string.h>
#include "log.h"
#include "stream.h"
#include "frame.h"
#include "session.h"
#include "x224.h"
#ifdef NTRDPS_TLS
#include "rdp.h"
#endif

#define X224_CR  0xE0
#define X224_CC  0xD0
#define X224_DR  0x80
#define X224_DT  0xF0

#define OUR_SRC_REF  0x1234
#define RDP_NEG_REQ  0x01
#define RDP_NEG_RSP  0x02

#define PROTOCOL_RDP  0x00000000UL
#define PROTOCOL_SSL  0x00000001UL

static void cookie_text(char *dst, unsigned int size, const unsigned char *p,
                        unsigned int n)
{
    unsigned int i;

    if (n > size - 1) {
        n = size - 1;
    }
    for (i = 0; i < n; i++) {
        dst[i] = (p[i] >= 0x20 && p[i] < 0x7F) ? (char) p[i] : '.';
    }
    dst[n] = '\0';
}

void x224_connect(SESSION *s, const unsigned char *p, unsigned int len)
{
    unsigned char cc[19];
    char cookie[48];
    OUTS o;
    INS in;
    unsigned int pos, end, start, src_ref;
    unsigned long protocols = 0, sel = PROTOCOL_RDP;
    int neg = 0;

    if (len < 11 || (p[5] & 0xF0) != X224_CR) {
        session_end(s, 0, "protocol error: the first packet is no X.224 "
                    "Connection Request (rdp-1 2.2)");
        return;
    }
    src_ref = ((unsigned int) p[8] << 8) | p[9];

    strcpy(cookie, "none");
    pos = 11;
    if (len - pos >= 7 && memcmp(p + pos, "Cookie:", 7) == 0) {
        for (end = pos; end + 1 < len; end++) {
            if (p[end] == 0x0D && p[end + 1] == 0x0A) {
                break;
            }
        }
        if (end + 1 >= len) {
            end = len;
        }
        start = pos + 7;
        while (start < end && p[start] == ' ') {
            start++;
        }
        cookie[0] = '"';
        cookie_text(cookie + 1, sizeof(cookie) - 2, p + start, end - start);
        strcat(cookie, "\"");
        pos = end < len ? end + 2 : len;
    }

    if (len - pos >= 8 && p[pos] == RDP_NEG_REQ) {
        neg = 1;
        in_init(&in, p + pos + 4, 4);
        protocols = in_u32le(&in);
#ifdef NTRDPS_TLS
        if ((protocols & PROTOCOL_SSL) && rdp_allow_tls()) {
            sel = PROTOCOL_SSL;
            s->client_protocols = protocols;
        }
#endif
    }
    if (neg) {
        log_msg(LOG_INFO, "session %s: X.224 Connection Request, cookie %s, "
                "RDP_NEG_REQ requestedProtocols 0x%lx; we select 0x%lx (%s)",
                s->peer, cookie, protocols, sel,
                sel == PROTOCOL_SSL ? "TLS" : "plain RDP");
    } else {
        log_msg(LOG_INFO, "session %s: X.224 Connection Request, cookie %s, "
                "no RDP_NEG_REQ", s->peer, cookie);
    }

    out_init(&o, cc, sizeof(cc));
    out_u8(&o, 3);
    out_u8(&o, 0);
    out_u16be(&o, neg ? 19 : 11);
    out_u8(&o, neg ? 14 : 6);
    out_u8(&o, X224_CC);
    out_u16be(&o, src_ref);
    out_u16be(&o, OUR_SRC_REF);
    out_u8(&o, 0);
    if (neg) {
        out_u8(&o, RDP_NEG_RSP);
        out_u8(&o, 0);
        out_u16le(&o, 8);
        out_u32le(&o, sel);
    }
#ifdef NTRDPS_TLS
    if (sel == PROTOCOL_SSL) {
        s->want_tls = 1;
    }
#endif
    if (session_send(s, cc, out_len(&o))) {
        s->state = ST_WAIT_CI;
    }
}

int x224_data(SESSION *s, const unsigned char *p, unsigned int len,
              INS *mcs)
{
    unsigned int li = p[4];

    switch (p[5] & 0xF0) {
    case X224_DT:
        if (li < 2 || 5 + li > len) {
            session_end(s, 0, "protocol error: X.224 Data with LI %u in a "
                        "packet of %u bytes (rdp-1 2.5)", li, len);
            return 0;
        }
        in_init(mcs, p + 5 + li, len - 5 - li);
        return 1;
    case X224_DR:
        session_end(s, 0, "the client sent X.224 Disconnect Request");
        return 0;
    }
    session_end(s, 0, "protocol error: X.224 code 0x%02x after the connect "
                "(rdp-1 2.5)", (unsigned int) p[5]);
    return 0;
}

void x224_begin(OUTS *o, unsigned char *buf, unsigned int cap)
{
    out_init(o, buf, cap);
    out_u8(o, 3);
    out_u8(o, 0);
    out_u16be(o, 0);
    out_u8(o, 2);
    out_u8(o, X224_DT);
    out_u8(o, 0x80);
}

int x224_end(SESSION *s, OUTS *o)
{
    if (o->fail || out_len(o) > FRAME_MAX) {
        session_end(s, 0, "internal error: a packet did not fit into its "
                    "buffer");
        return 0;
    }
    out_put_u16be(o, 2, out_len(o));
    return session_send(s, o->data, out_len(o));
}
