/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <string.h>
#include "log.h"
#include "stream.h"
#include "session.h"
#include "x224.h"
#include "gcc.h"
#include "rdp.h"
#include "mcs.h"

static const unsigned char domain_params[28] = {
    0x30, 0x1A,
    0x02, 0x01, 0x22, 0x02, 0x01, 0x03, 0x02, 0x01, 0x00, 0x02, 0x01, 0x01,
    0x02, 0x01, 0x00, 0x02, 0x01, 0x01, 0x02, 0x03, 0x00, 0xFF, 0xF8,
    0x02, 0x01, 0x02
};

static unsigned int ber_read_len(INS *in)
{
    unsigned int b = in_u8(in);

    if (b < 0x80) {
        return b;
    }
    if (b == 0x81) {
        return in_u8(in);
    }
    if (b == 0x82) {
        return in_u16be(in);
    }
    in->fail = 1;
    return 0;
}

static unsigned int ber_len_size(unsigned int n)
{
    return n < 0x80 ? 1 : (n < 0x100 ? 2 : 3);
}

static void ber_write_len(OUTS *o, unsigned int n)
{
    if (n >= 0x100) {
        out_u8(o, 0x82);
        out_u16be(o, n);
    } else if (n >= 0x80) {
        out_u8(o, 0x81);
        out_u8(o, n);
    } else {
        out_u8(o, n);
    }
}

static void ber_skip(INS *in, unsigned int tag)
{
    if (in_u8(in) != tag) {
        in->fail = 1;
        return;
    }
    in_skip(in, ber_read_len(in));
}

static void connect_response(SESSION *s)
{
    unsigned char buf[256];
    OUTS o;
    unsigned int gl;
    unsigned int body;

    gl = gcc_response_len(s->channels);
#ifdef NTRDPS_TLS
    if (s->tls != NULL) {
        gl = gcc_response_len_tls(s->channels);
    }
#endif
    body = 3 + 3 + sizeof(domain_params) + 1 + ber_len_size(gl) + gl;

    x224_begin(&o, buf, sizeof(buf));
    out_u8(&o, 0x7F);
    out_u8(&o, 0x66);
    ber_write_len(&o, body);
    out_u8(&o, 0x0A);
    out_u8(&o, 0x01);
    out_u8(&o, 0x00);
    out_u8(&o, 0x02);
    out_u8(&o, 0x01);
    out_u8(&o, 0x00);
    out_bytes(&o, domain_params, sizeof(domain_params));
    out_u8(&o, 0x04);
    ber_write_len(&o, gl);
#ifdef NTRDPS_TLS
    if (s->tls != NULL) {
        gcc_write_response_tls(&o, s->channels, s->client_protocols);
    } else
#endif
        gcc_write_response(&o, s->channels);
    if (x224_end(s, &o)) {
        s->state = ST_WAIT_DOMAIN;
    }
}

static void connect_initial(SESSION *s, INS *in)
{
    INS ci, ud;

    if (in_u8(in) != 0x7F || in_u8(in) != 0x65) {
        session_end(s, 0, "protocol error: no MCS Connect-Initial "
                    "(rdp-1 3.2)");
        return;
    }
    in_sub(in, &ci, ber_read_len(in));
    ber_skip(&ci, 0x04);
    ber_skip(&ci, 0x04);
    ber_skip(&ci, 0x01);
    ber_skip(&ci, 0x30);
    ber_skip(&ci, 0x30);
    ber_skip(&ci, 0x30);
    if (in_u8(&ci) != 0x04) {
        ci.fail = 1;
    }
    in_sub(&ci, &ud, ber_read_len(&ci));
    if (in->fail || ci.fail) {
        session_end(s, 0, "protocol error: bad MCS Connect-Initial "
                    "(rdp-1 3.2)");
        return;
    }
    if (!gcc_read_request(s, in_ptr(&ud), in_left(&ud))) {
        return;
    }
    s->depth = gcc_session_depth(s, s->console_depth);
    log_msg(LOG_INFO, "session %s: %u channels; session depth %d bpp "
            "(console %d bpp)", s->peer, s->channels, s->depth,
            s->console_depth);
    connect_response(s);
}

#define DOM_EDRQ 1
#define DOM_DPUM 8
#define DOM_AURQ 10
#define DOM_CJRQ 14
#define DOM_SDRQ 25

static const char *domain_name(unsigned int type)
{
    switch (type) {
    case DOM_EDRQ: return "Erect Domain Request";
    case DOM_AURQ: return "Attach User Request";
    case DOM_CJRQ: return "Channel Join Request";
    case DOM_SDRQ: return "Send Data Request";
    }
    return NULL;
}

static void disconnect_ultimatum(SESSION *s, INS *in)
{
    static const char *const names[5] = {
        "domain disconnected", "provider initiated", "token purged",
        "user requested", "channel purged"
    };
    unsigned int reason = (in_u8(in) & 3) << 1;

    reason |= in_u8(in) >> 7;
    session_end(s, 0, "the client sent MCS Disconnect Provider Ultimatum, "
                "reason %u (%s)", reason, reason < 5 ? names[reason] : "?");
}

static void attach_user(SESSION *s)
{
    unsigned char buf[16];
    OUTS o;

    s->user_id = MCS_IO_CHANNEL + 1 + s->channels;
    x224_begin(&o, buf, sizeof(buf));
    out_u8(&o, 0x2E);
    out_u8(&o, 0x00);
    out_u16be(&o, s->user_id - MCS_USER_BASE);
    if (x224_end(s, &o)) {
        log_msg(LOG_INFO, "session %s: MCS user %u attached", s->peer,
                s->user_id);
        s->state = ST_WAIT_JOINS;
    }
}

static void send_data(SESSION *s, INS *in)
{
    unsigned int len = in_left(in);
    unsigned int ch, prio;
    INS data;

    in_skip(in, 3);
    ch = in_u16be(in);
    prio = in_u8(in);
    in_sub(in, &data, per_read_len(in));
    if (in->fail) {
        session_end(s, 0, "protocol error: MCS Send Data Request of %u bytes "
                    "with a bad length (rdp-1 5.1)", len);
        return;
    }
    if (prio != 0x70) {
        log_msg(LOG_DEBUG, "session %s: MCS Send Data Request with priority "
                "byte 0x%02x, not 0x70", s->peer, prio);
    }
    if (ch != MCS_IO_CHANNEL) {
        log_msg(LOG_DEBUG, "session %s: %u bytes on channel %u dropped",
                s->peer, in_left(&data), ch);
        return;
    }
    rdp_data(s, &data);
}

int mcs_send_dpum(SESSION *s, unsigned int reason)
{
    unsigned char buf[16];
    OUTS o;

    x224_begin(&o, buf, sizeof(buf));
    out_u8(&o, 0x20 | (reason >> 1));
    out_u8(&o, (reason & 1) << 7);
    return x224_end(s, &o);
}

void mcs_sdin_begin(OUTS *o, unsigned char *buf, unsigned int cap)
{
    x224_begin(o, buf, cap);
    out_u8(o, 0x68);
    out_u16be(o, MCS_SERVER_USER - MCS_USER_BASE);
    out_u16be(o, MCS_IO_CHANNEL);
    out_u8(o, 0x70);
    out_reserve(o, 2);
}

int mcs_sdin_end(SESSION *s, OUTS *o)
{
    unsigned int n;

    if (o->fail) {
        return x224_end(s, o);
    }
    n = out_len(o) - MCS_SDIN_HEADER;
    if (n > MCS_SDIN_MAX) {
        o->fail = 1;
    } else if (n < 0x80) {
        memmove(o->data + MCS_SDIN_HEADER - 1, o->data + MCS_SDIN_HEADER, n);
        o->len--;
        out_put_u8(o, MCS_SDIN_HEADER - 2, n);
    } else {
        out_put_u8(o, MCS_SDIN_HEADER - 2, 0x80 | (n >> 8));
        out_put_u8(o, MCS_SDIN_HEADER - 1, n & 0xFF);
    }
    return x224_end(s, o);
}

static void channel_join(SESSION *s, INS *in)
{
    unsigned char buf[16];
    OUTS o;
    unsigned int len = in_left(in);
    unsigned int initiator, ch;
    const char *what;

    if (len < 5) {
        session_end(s, 0, "protocol error: MCS Channel Join Request of %u "
                    "bytes; expected 5 (rdp-1 4.4)", len);
        return;
    }
    in_skip(in, 1);
    initiator = in_u16be(in);
    ch = in_u16be(in);

    if (ch == s->user_id) {
        what = "the user channel";
    } else if (ch == MCS_IO_CHANNEL) {
        what = "the I/O channel";
    } else if (ch > MCS_IO_CHANNEL && ch <= MCS_IO_CHANNEL + s->channels) {
        what = "a virtual channel";
    } else {
        what = "not ours; confirmed anyway";
    }
    log_msg(LOG_INFO, "session %s: MCS user %u joins channel %u (%s)",
            s->peer, initiator + MCS_USER_BASE, ch, what);

    x224_begin(&o, buf, sizeof(buf));
    out_u8(&o, 0x3E);
    out_u8(&o, 0x00);
    out_u16be(&o, initiator);
    out_u16be(&o, ch);
    out_u16be(&o, ch);
    x224_end(s, &o);
}

void mcs_pdu(SESSION *s, INS *in)
{
    unsigned int len = in_left(in);
    unsigned int type;

    if (len == 0) {
        session_end(s, 0, "protocol error: X.224 Data without an MCS PDU "
                    "(rdp-1 4.1)");
        return;
    }
    type = *in_ptr(in) >> 2;
    if (type == DOM_DPUM) {
        disconnect_ultimatum(s, in);
    } else if (s->state == ST_WAIT_CI) {
        connect_initial(s, in);
    } else if (type == DOM_EDRQ && s->state == ST_WAIT_DOMAIN) {
        log_msg(LOG_DEBUG, "session %s: MCS Erect Domain Request: no answer "
                "(rdp-1 4.2)", s->peer);
    } else if (type == DOM_AURQ && s->state == ST_WAIT_DOMAIN) {
        attach_user(s);
    } else if (type == DOM_CJRQ && s->state == ST_WAIT_JOINS) {
        channel_join(s, in);
    } else if (type == DOM_SDRQ && s->state >= ST_WAIT_JOINS) {
        send_data(s, in);
    } else if (domain_name(type)) {
        log_msg(LOG_WARN, "session %s: MCS %s in state %s: ignored "
                "(rdp-1 12)", s->peer, domain_name(type),
                rdp_state_name(s->state));
    } else {
        log_msg(LOG_WARN, "session %s: unknown MCS PDU type %u (first byte "
                "0x%02x, %u bytes): ignored (rdp-1 4.1)", s->peer, type,
                *in_ptr(in), len);
    }
}
