/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <string.h>
#include "log.h"
#include "stream.h"
#include "session.h"
#include "mcs.h"
#include "gcc.h"

#define CS_CORE     0xC001
#define CS_SECURITY 0xC002
#define CS_NET      0xC003
#define SC_CORE     0x0C01
#define SC_SECURITY 0x0C02
#define SC_NET      0x0C03

#define RDP5_VERSION 0x00080004UL

static const unsigned char t124_key[7] = {
    0x00, 0x05, 0x00, 0x14, 0x7C, 0x00, 0x01
};

static const unsigned char ccr_head[9] = {
    0x14, 0x76, 0x0A, 0x01, 0x01, 0x00, 0x01, 0xC0, 0x00
};

unsigned int per_read_len(INS *in)
{
    unsigned int b = in_u8(in);

    if (b & 0x80) {
        return ((b & 0x3F) << 8) | in_u8(in);
    }
    return b;
}

static void read_core(SESSION *s, INS *b, unsigned int blen)
{
    unsigned char name16[32];
    char name[17];
    unsigned int color, i;
    unsigned long build;

    s->core_len = blen;
    s->client_version = in_u32le(b);
    s->client_width = in_u16le(b);
    s->client_height = in_u16le(b);
    color = in_u16le(b);
    in_skip(b, 2);
    s->kbd_layout = in_u32le(b);
    build = in_u32le(b);
    in_bytes(b, name16, 32);
    s->kbd_type = in_u32le(b);
    s->kbd_subtype = in_u32le(b);
    s->kbd_fkeys = in_u32le(b);
    in_skip(b, 64);
    if (blen >= 144) {
        in_skip(b, 2 + 2 + 4);
        s->high_depth = in_u16le(b);
        s->supported_depths = in_u16le(b);
    }
    if (blen >= 146) {
        s->early_flags = in_u16le(b);
    }
    if (b->fail) {
        log_msg(LOG_WARN, "session %s: CS_CORE of %u bytes is too short; "
                "missing fields count as 0", s->peer, blen);
    }

    for (i = 0; i < 16; i++) {
        unsigned int c = name16[2 * i] | (name16[2 * i + 1] << 8);

        if (c == 0) {
            break;
        }
        name[i] = (c >= 0x20 && c < 0x7F) ? (char) c : '?';
    }
    name[i] = '\0';
    log_msg(LOG_INFO, "session %s: CS_CORE %u bytes: version 0x%08lx, "
            "client \"%s\", build %lu, asks %u x %u, colorDepth 0x%04x, "
            "highColorDepth %u, supportedColorDepths 0x%04x, "
            "earlyCapabilityFlags 0x%04x", s->peer, blen, s->client_version,
            name, build, s->client_width, s->client_height, color,
            s->high_depth, s->supported_depths, s->early_flags);
    log_msg(LOG_INFO, "session %s: keyboard layout 0x%08lx, type %lu, "
            "subtype %lu, function keys %lu", s->peer, s->kbd_layout,
            s->kbd_type, s->kbd_subtype, s->kbd_fkeys);
}

static void read_security(SESSION *s, INS *b)
{
    unsigned long methods = in_u32le(b);
    unsigned long ext = in_u32le(b);

    log_msg(LOG_INFO, "session %s: CS_SECURITY encryptionMethods 0x%lx, "
            "ext 0x%lx (we always answer: no encryption)", s->peer, methods,
            ext);
}

static int read_net(SESSION *s, INS *b)
{
    unsigned long n = in_u32le(b);
    unsigned char name[9];
    unsigned long options;
    unsigned int i;

    if (b->fail || n > GCC_MAX_CHANNELS || in_left(b) < n * 12) {
        session_end(s, 0, "protocol error: CS_NET with %lu channels in %u "
                    "bytes; at most %d (rdp-1 3.4)", n, in_left(b),
                    GCC_MAX_CHANNELS);
        return 0;
    }
    s->channels = (unsigned int) n;
    for (i = 0; i < n; i++) {
        in_bytes(b, name, 8);
        name[8] = '\0';
        options = in_u32le(b);
        log_msg(LOG_INFO, "session %s: channel %u \"%s\", options 0x%08lx: "
                "id %u, its data is dropped", s->peer, i, (char *) name,
                options, MCS_IO_CHANNEL + 1 + i);
    }
    return 1;
}

int gcc_read_request(SESSION *s, const unsigned char *p, unsigned int len)
{
    INS in, blk;
    unsigned int i, n, type, blen;

    for (i = 0; i + 4 <= len && i + 4 <= 32; i++) {
        if (memcmp(p + i, "Duca", 4) == 0) {
            break;
        }
    }
    if (i + 4 > len || i + 4 > 32) {
        session_end(s, 0, "protocol error: no \"Duca\" in the GCC data "
                    "(rdp-1 3.2)");
        return 0;
    }
    in_init(&in, p + i + 4, len - i - 4);
    n = per_read_len(&in);
    if (in.fail) {
        session_end(s, 0, "protocol error: GCC data ends after \"Duca\"");
        return 0;
    }
    if (n < in_left(&in)) {
        in.len = in.pos + n;
    }
    while (in_left(&in) >= 4) {
        type = in_u16le(&in);
        blen = in_u16le(&in);
        if (blen < 4 || blen - 4 > in_left(&in)) {
            log_msg(LOG_WARN, "session %s: client data block 0x%04x with "
                    "length %u: the rest is ignored", s->peer, type, blen);
            break;
        }
        in_sub(&in, &blk, blen - 4);
        switch (type) {
        case CS_CORE:
            read_core(s, &blk, blen);
            break;
        case CS_SECURITY:
            read_security(s, &blk);
            break;
        case CS_NET:
            if (!read_net(s, &blk)) {
                return 0;
            }
            break;
        default:
            log_msg(LOG_DEBUG, "session %s: client data block 0x%04x "
                    "(%u bytes) ignored", s->peer, type, blen);
            break;
        }
    }
    if (s->core_len == 0) {
        log_msg(LOG_WARN, "session %s: no CS_CORE block; we use 8 bpp",
                s->peer);
    }
    return 1;
}

static unsigned int data_len(unsigned int n)
{
    return 8 + (8 + 2 * n + (n & 1) * 2) + 12;
}

unsigned int gcc_response_len(unsigned int n)
{
    return 7 + 1 + 14 + data_len(n);
}

void gcc_write_response(OUTS *o, unsigned int n)
{
    unsigned int udl = data_len(n);
    unsigned int i;

    out_bytes(o, t124_key, 7);
    out_u8(o, 14 + udl);
    out_bytes(o, ccr_head, 9);
    out_bytes(o, "McDn", 4);
    out_u8(o, udl);

    out_u16le(o, SC_CORE);
    out_u16le(o, 8);
    out_u32le(o, RDP5_VERSION);

    out_u16le(o, SC_NET);
    out_u16le(o, 8 + 2 * n + (n & 1) * 2);
    out_u16le(o, MCS_IO_CHANNEL);
    out_u16le(o, n);
    for (i = 0; i < n; i++) {
        out_u16le(o, MCS_IO_CHANNEL + 1 + i);
    }
    if (n & 1) {
        out_u16le(o, 0);
    }

    out_u16le(o, SC_SECURITY);
    out_u16le(o, 12);
    out_u32le(o, 0);
    out_u32le(o, 0);
}

#ifdef NTRDPS_TLS
static unsigned int data_len_tls(unsigned int n)
{
    return 16 + (8 + 2 * n + (n & 1) * 2) + 12;
}

unsigned int gcc_response_len_tls(unsigned int n)
{
    return 7 + 1 + 14 + data_len_tls(n);
}

void gcc_write_response_tls(OUTS *o, unsigned int n, unsigned long protocols)
{
    unsigned int udl = data_len_tls(n);
    unsigned int i;

    out_bytes(o, t124_key, 7);
    out_u8(o, 14 + udl);
    out_bytes(o, ccr_head, 9);
    out_bytes(o, "McDn", 4);
    out_u8(o, udl);

    out_u16le(o, SC_CORE);
    out_u16le(o, 16);
    out_u32le(o, RDP5_VERSION);
    out_u32le(o, protocols);
    out_u32le(o, 0);

    out_u16le(o, SC_NET);
    out_u16le(o, 8 + 2 * n + (n & 1) * 2);
    out_u16le(o, MCS_IO_CHANNEL);
    out_u16le(o, n);
    for (i = 0; i < n; i++) {
        out_u16le(o, MCS_IO_CHANNEL + 1 + i);
    }
    if (n & 1) {
        out_u16le(o, 0);
    }

    out_u16le(o, SC_SECURITY);
    out_u16le(o, 12);
    out_u32le(o, 0);
    out_u32le(o, 0);
}
#endif

int gcc_session_depth(const SESSION *s, int d)
{
    unsigned int w = s->high_depth, m = s->supported_depths;

    if (d == 32) {
        d = 24;
    }
    if (s->core_len < 144) {
        return 8;
    }
    switch (d) {
    case 15:
        return (w == 15 || (m & 0x0004)) ? 15 : 8;
    case 16:
        return (w == 16 || (m & 0x0002)) ? 16 : 8;
    case 24:
        return (w == 24 || (m & 0x0001)) ? 24 : 8;
    }
    return 8;
}
