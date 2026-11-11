/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <winsock.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "bearssl.h"
#include "cert.h"
#include "tls.h"

struct tls_conn {
    SOCKET sock;
    int    eof;
    int    sockerr;
    long   events;
    br_ssl_server_context sc;
    unsigned char ibuf[BR_SSL_BUFSIZE_INPUT];
    unsigned char obuf[BR_SSL_BUFSIZE_OUTPUT];
};

static const uint16_t g_suites[] = {
    BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256
};

static br_hmac_drbg_context g_rng;
static CRITICAL_SECTION     g_lock;
static int                  g_ready;

int tls_startup(void)
{
    struct {
        FILETIME      ft;
        MEMORYSTATUS  ms;
        LARGE_INTEGER freq;
        DWORD         pid, tid, tick;
        void         *stack;
        DWORD         jitter[128];
    } s;
    LARGE_INTEGER t0, t1;
    SYSTEMTIME st;
    int n;

    if (g_ready) {
        return 1;
    }
    memset(&s, 0, sizeof(s));
    GetSystemTime(&st);
    SystemTimeToFileTime(&st, &s.ft);
    s.ms.dwLength = sizeof(s.ms);
    GlobalMemoryStatus(&s.ms);
    QueryPerformanceFrequency(&s.freq);
    s.pid = GetCurrentProcessId();
    s.tid = GetCurrentThreadId();
    s.tick = GetTickCount();
    s.stack = &s;
    for (n = 0; n < 128; n++) {
        QueryPerformanceCounter(&t0);
        Sleep(0);
        QueryPerformanceCounter(&t1);
        s.jitter[n] = (DWORD) (t1.LowPart - t0.LowPart) ^ GetTickCount();
    }
    br_hmac_drbg_init(&g_rng, &br_sha256_vtable, &s, sizeof(s));
    InitializeCriticalSection(&g_lock);
    g_ready = 1;
    return 1;
}

void tls_cleanup(void)
{
    if (g_ready) {
        DeleteCriticalSection(&g_lock);
        g_ready = 0;
    }
}

static void draw_seed(unsigned char *seed, size_t len)
{
    struct {
        LARGE_INTEGER pc;
        DWORD         tick;
        DWORD         tid;
    } e;

    memset(&e, 0, sizeof(e));
    QueryPerformanceCounter(&e.pc);
    e.tick = GetTickCount();
    e.tid = GetCurrentThreadId();
    EnterCriticalSection(&g_lock);
    br_hmac_drbg_update(&g_rng, &e, sizeof(e));
    br_hmac_drbg_generate(&g_rng, seed, len);
    LeaveCriticalSection(&g_lock);
}

void tls_random(void *out, unsigned int len)
{
    if (!g_ready) {
        tls_startup();
    }
    draw_seed((unsigned char *) out, len);
}

static void init_server(struct tls_conn *t)
{
    br_ssl_engine_context *eng = &t->sc.eng;
    unsigned char seed[32];

    br_ssl_server_zero(&t->sc);
    br_ssl_engine_set_versions(eng, BR_TLS12, BR_TLS12);
    br_ssl_engine_set_suites(eng, g_suites, sizeof(g_suites) / sizeof(g_suites[0]));
    br_ssl_engine_set_ec(eng, &br_ec_p256_m15);
    br_ssl_server_set_single_ec(&t->sc, ntrdps_tls_chain, ntrdps_tls_chain_len,
        &ntrdps_tls_key, BR_KEYTYPE_SIGN, 0, &br_ec_p256_m15,
        br_ecdsa_i15_sign_asn1);
    br_ssl_engine_set_hash(eng, br_sha256_ID, &br_sha256_vtable);
    br_ssl_engine_set_prf_sha256(eng, &br_tls12_sha256_prf);
    br_ssl_engine_set_aes_ctr(eng, &br_aes_ct_ctr_vtable);
    br_ssl_engine_set_ghash(eng, &br_ghash_ctmul32);
    br_ssl_engine_set_gcm(eng, &br_sslrec_in_gcm_vtable, &br_sslrec_out_gcm_vtable);
    br_ssl_engine_set_buffers_bidi(eng, t->ibuf, sizeof(t->ibuf),
        t->obuf, sizeof(t->obuf));
    br_ssl_engine_add_flags(eng, BR_OPT_NO_RENEGOTIATION);

    draw_seed(seed, sizeof(seed));
    br_ssl_engine_inject_entropy(eng, seed, sizeof(seed));
    br_ssl_server_reset(&t->sc);
}

TLS *tls_server_start(SOCKET sock)
{
    struct tls_conn *t;

    if (!g_ready) {
        return NULL;
    }
    t = (struct tls_conn *) malloc(sizeof(*t));
    if (t == NULL) {
        return NULL;
    }
    memset(t, 0, sizeof(*t));
    t->sock = sock;
    init_server(t);
    if (br_ssl_engine_last_error(&t->sc.eng) != BR_ERR_OK) {
        free(t);
        return NULL;
    }
    return t;
}

static int send_records(struct tls_conn *t)
{
    unsigned char *buf;
    size_t len;
    int n;

    buf = br_ssl_engine_sendrec_buf(&t->sc.eng, &len);
    n = send(t->sock, (char *) buf, (int) len, 0);
    if (n > 0) {
        br_ssl_engine_sendrec_ack(&t->sc.eng, (size_t) n);
        return 1;
    }
    if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
        t->events |= FD_WRITE;
        return 0;
    }
    t->sockerr = 1;
    return -1;
}

static int run(struct tls_conn *t, unsigned want)
{
    br_ssl_engine_context *eng = &t->sc.eng;
    unsigned state;
    unsigned char *buf;
    size_t len;
    int n;

    for (;;) {
        t->events = 0;
        state = br_ssl_engine_current_state(eng);
        if (state & BR_SSL_CLOSED) {
            return -1;
        }
        if (state & BR_SSL_SENDREC) {
            n = send_records(t);
            if (n < 0) {
                return -1;
            }
            if (n > 0) {
                continue;
            }
        }
        if (state & want) {
            return 1;
        }
        if (state & BR_SSL_RECVREC) {
            buf = br_ssl_engine_recvrec_buf(eng, &len);
            n = recv(t->sock, (char *) buf, (int) len, 0);
            if (n > 0) {
                br_ssl_engine_recvrec_ack(eng, (size_t) n);
                continue;
            }
            if (n == 0) {
                t->eof = 1;
                return -1;
            }
            if (WSAGetLastError() != WSAEWOULDBLOCK) {
                t->sockerr = 1;
                return -1;
            }
            t->events |= FD_READ;
        }
        return t->events ? 0 : -1;
    }
}

int tls_read(TLS *t, void *buf, int len)
{
    unsigned char *p;
    size_t n;
    int r;

    r = run(t, BR_SSL_RECVAPP);
    if (r == 0) {
        return TLS_WOULDBLOCK;
    }
    if (r < 0) {
        if (t->eof || (!t->sockerr &&
                br_ssl_engine_last_error(&t->sc.eng) == BR_ERR_OK)) {
            return 0;
        }
        return -1;
    }
    p = br_ssl_engine_recvapp_buf(&t->sc.eng, &n);
    if (n > (size_t) len) {
        n = (size_t) len;
    }
    memcpy(buf, p, n);
    br_ssl_engine_recvapp_ack(&t->sc.eng, n);
    return (int) n;
}

int tls_write(TLS *t, const void *buf, int len)
{
    unsigned char *p;
    size_t n;
    int r;

    r = run(t, BR_SSL_SENDAPP);
    if (r == 0) {
        return TLS_WOULDBLOCK;
    }
    if (r < 0) {
        return -1;
    }
    p = br_ssl_engine_sendapp_buf(&t->sc.eng, &n);
    if (n > (size_t) len) {
        n = (size_t) len;
    }
    memcpy(p, buf, n);
    br_ssl_engine_sendapp_ack(&t->sc.eng, n);
    br_ssl_engine_flush(&t->sc.eng, 0);
    for (;;) {
        unsigned state = br_ssl_engine_current_state(&t->sc.eng);
        if (!(state & BR_SSL_SENDREC)) {
            break;
        }
        t->events = 0;
        if (send_records(t) <= 0) {
            break;
        }
    }
    return (int) n;
}

int tls_flush(TLS *t)
{
    unsigned state;
    int n;

    br_ssl_engine_flush(&t->sc.eng, 0);
    for (;;) {
        state = br_ssl_engine_current_state(&t->sc.eng);
        if (state & BR_SSL_CLOSED) {
            return -1;
        }
        if (!(state & BR_SSL_SENDREC)) {
            return 0;
        }
        t->events = 0;
        n = send_records(t);
        if (n <= 0) {
            return n == 0 ? TLS_WOULDBLOCK : -1;
        }
    }
}

long tls_events(TLS *t)
{
    return t ? t->events : 0;
}

int tls_pending(TLS *t)
{
    return t != NULL &&
        (br_ssl_engine_current_state(&t->sc.eng) & BR_SSL_RECVAPP) != 0;
}

const char *tls_error(TLS *t)
{
    static char msg[64];
    int err;

    if (t == NULL) {
        return "";
    }
    if (t->sockerr) {
        return "network error";
    }
    if (t->eof) {
        return "the client closed the connection";
    }
    err = br_ssl_engine_last_error(&t->sc.eng);
    if (err >= BR_ERR_RECV_FATAL_ALERT && err < BR_ERR_SEND_FATAL_ALERT) {
        sprintf(msg, "the client sent TLS alert %d",
            err - BR_ERR_RECV_FATAL_ALERT);
    } else {
        sprintf(msg, "TLS error %d", err);
    }
    return msg;
}

void tls_free(TLS *t)
{
    if (t != NULL) {
        free(t);
    }
}
