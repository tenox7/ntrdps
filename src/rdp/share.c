/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <string.h>
#include "log.h"
#include "stream.h"
#include "session.h"
#include "mcs.h"
#include "rdp.h"
#include "caps.h"
#include "update.h"
#include "pointer.h"
#include "inputpdu.h"
#include "mppc.h"
#include "share.h"

#define FLOW_MARKER 0x8000
#define FLOW_PDU    8

#define CTRL_REQUEST   1
#define CTRL_GRANTED   2
#define CTRL_COOPERATE 4

#define FONTLIST_LAST 0x0002

static void data_header(OUTS *o, unsigned char *buf, unsigned int cap,
                        unsigned int source, unsigned long share_id,
                        unsigned int stream)
{
    mcs_sdin_begin(o, buf, cap);
    out_u16le(o, 0);
    out_u16le(o, SHARE_VERSION | SHARE_DATA);
    out_u16le(o, source);
    out_u32le(o, share_id);
    out_u8(o, 0);
    out_u8(o, stream);
    out_u16le(o, 0);
    out_u8(o, 0);
    out_u8(o, 0);
    out_u16le(o, 0);
}

void share_begin(SESSION *s, OUTS *o, unsigned char *buf, unsigned int cap)
{
    data_header(o, buf, cap, MCS_SERVER_USER, s->share_id,
                2);
}

int share_send_error_info(SESSION *s, unsigned long code)
{
    unsigned char buf[64];
    OUTS o;

    data_header(&o, buf, sizeof(buf), 0, 0, 1);
    out_u32le(&o, code);
    return share_end(s, &o, T2_SET_ERROR_INFO);
}

int share_deactivate_all(SESSION *s)
{
    unsigned char buf[64];
    OUTS o;

    mcs_sdin_begin(&o, buf, sizeof(buf));
    out_u16le(&o, 13);
    out_u16le(&o, SHARE_VERSION | SHARE_DEACTIVATE_ALL);
    out_u16le(&o, MCS_SERVER_USER);
    out_u32le(&o, s->share_id);
    out_u16le(&o, 1);
    out_u8(&o, 0);
    return mcs_sdin_end(s, &o);
}

int share_end(SESSION *s, OUTS *o, unsigned int type2)
{
    unsigned int n = out_len(o) - MCS_SDIN_HEADER;
    unsigned int body, ctype, clen;
    const unsigned char *cbody;

    if (!o->fail) {
        out_put_u16le(o, MCS_SDIN_HEADER, n);
        out_put_u16le(o, MCS_SDIN_HEADER + 12, n);
        out_put_u8(o, MCS_SDIN_HEADER + 14, type2);
        body = n - SHARE_DATA_HEADER;
        if (s->mppc != NULL && s->state == ST_ACTIVE &&
            (type2 == T2_UPDATE || type2 == T2_POINTER) &&
            body >= MPPC_MIN_BODY) {
            ctype = mppc_compress(s->mppc, o->data + MCS_SDIN_HEADER +
                                  SHARE_DATA_HEADER, body, &cbody, &clen);
            if (ctype != 0) {
                memcpy(o->data + MCS_SDIN_HEADER + SHARE_DATA_HEADER,
                       cbody, clen);
                o->len = MCS_SDIN_HEADER + SHARE_DATA_HEADER + clen;
                n = SHARE_DATA_HEADER + clen;
                out_put_u16le(o, MCS_SDIN_HEADER, n);
                out_put_u8(o, MCS_SDIN_HEADER + 15, ctype);
                out_put_u16le(o, MCS_SDIN_HEADER + 16, n);
            }
        }
    }
    return mcs_sdin_end(s, o);
}

void share_start_compression(SESSION *s, int asked)
{
    int type, max_type;

    if (!rdp_compression(&max_type)) {
        log_msg(LOG_INFO, "session %s: bulk compression: off (the setting "
                "Compression is 0)", s->peer);
        return;
    }
    type = asked > 0 ? 1 : 0;
    if (type > max_type) {
        type = max_type;
    }
    s->mppc = mppc_new(type);
    if (s->mppc == NULL) {
        log_msg(LOG_WARN, "session %s: no memory for bulk compression; "
                "the session sends everything raw", s->peer);
        return;
    }
    log_msg(LOG_INFO, "session %s: bulk compression: type %d (%s history; "
            "the client asked for type %d)", s->peer, type,
            type ? "64 KB" : "8 KB", asked);
}

void share_restart_compression(SESSION *s)
{
    if (s->mppc != NULL) {
        mppc_restart(s->mppc);
    }
}

void share_done(SESSION *s)
{
    MPPC_ENC *e = s->mppc;

    if (e == NULL) {
        return;
    }
    if (e->bodies > 0) {
        log_msg(LOG_INFO, "session %s: bulk compression type %d: %lu bodies "
                "of 33 bytes and more, %.0f bytes went out as %.0f bytes "
                "(%.0f%%); %lu compressed, %lu raw because not smaller, %lu "
                "raw because larger than %u bytes; %lu restarts; %.1f ms",
                s->peer, e->type, e->bodies, e->bytes_in, e->bytes_out,
                e->bytes_in > 0 ? e->bytes_out * 100.0 / e->bytes_in : 0.0,
                e->packed, e->expanded, e->raw_big, e->max_in, e->restarts,
                e->ms);
    }
    mppc_free(e);
    s->mppc = NULL;
}

static void send_synchronize(SESSION *s)
{
    unsigned char buf[64];
    OUTS o;

    share_begin(s, &o, buf, sizeof(buf));
    out_u16le(&o, 1);
    out_u16le(&o, MCS_SERVER_USER);
    share_end(s, &o, T2_SYNCHRONIZE);
}

static void send_control(SESSION *s, unsigned int action,
                         unsigned int grant_id, unsigned long control_id)
{
    unsigned char buf[64];
    OUTS o;

    share_begin(s, &o, buf, sizeof(buf));
    out_u16le(&o, action);
    out_u16le(&o, grant_id);
    out_u32le(&o, control_id);
    share_end(s, &o, T2_CONTROL);
}

static void send_font_map(SESSION *s)
{
    unsigned char buf[64];
    OUTS o;

    share_begin(s, &o, buf, sizeof(buf));
    out_u16le(&o, 0);
    out_u16le(&o, 0);
    out_u16le(&o, 3);
    out_u16le(&o, 4);
    if (share_end(s, &o, T2_FONTMAP)) {
        s->state = ST_ACTIVE;
        log_msg(LOG_INFO, "session %s: ACTIVE: share 0x%08lx, %u x %u, "
                "%d bpp", s->peer, s->share_id, s->width, s->height,
                s->depth);
        pointer_reset(s);
        update_screen(s);
    }
}

static const char *type2_name(unsigned int t)
{
    switch (t) {
    case T2_CONTROL:          return "Control";
    case T2_INPUT:            return "Input";
    case T2_SYNCHRONIZE:      return "Synchronize";
    case T2_REFRESH_RECT:     return "Refresh Rect";
    case T2_SUPPRESS_OUTPUT:  return "Suppress Output";
    case T2_SHUTDOWN_REQUEST: return "Shutdown Request";
    case T2_FONTLIST:         return "Font List";
    case T2_PERSISTENT_LIST:  return "Persistent Key List";
    case 44:                  return "Bitmap Cache Error";
    case 46:                  return "Offscreen Cache Error";
    case 48:                  return "Draw Nine Grid Error";
    case 49:                  return "Draw GDI+ Error";
    }
    return NULL;
}

static long s16(unsigned int v)
{
    return v >= 0x8000 ? (long) v - 0x10000L : (long) v;
}

static void refresh_rect(SESSION *s, INS *in)
{
    unsigned int n, i, room;
    long left, top, right, bottom;

    n = in_u8(in);
    in_skip(in, 3);
    room = in->fail ? 0 : in_left(in) / 8;
    if (room < n) {
        log_msg(LOG_WARN, "session %s: Refresh Rect for %u areas has room "
                "for %u: only those are used (rdp-2 5.1 rule 4)", s->peer,
                n, room);
        n = room;
    }
    for (i = 0; i < n; i++) {
        left = s16(in_u16le(in));
        top = s16(in_u16le(in));
        right = s16(in_u16le(in)) + 1;
        bottom = s16(in_u16le(in)) + 1;
        log_msg(LOG_DEBUG, "session %s: Refresh Rect (%ld,%ld)-(%ld,%ld) "
                "inclusive%s", s->peer, left, top, right - 1, bottom - 1,
                update_area(s, left, top, right, bottom)
                ? "" : ": empty on the desktop, skipped");
    }
}

static void suppress_output(SESSION *s, INS *in)
{
    unsigned int allow = in_u8(in);

    if (in->fail) {
        log_msg(LOG_WARN, "session %s: Suppress Output without a body: "
                "ignored", s->peer);
        return;
    }
    if (allow == 0) {
        s->suppressed = 1;
        log_msg(LOG_DEBUG, "session %s: Suppress Output: no screen updates "
                "now", s->peer);
        return;
    }
    if (allow > 128) {
        log_msg(LOG_WARN, "session %s: Suppress Output: first byte %u, "
                "taken as 1 (rdp-2 5.2)", s->peer, allow);
    }
    s->suppressed = 0;
    log_msg(LOG_DEBUG, "session %s: Suppress Output: screen updates again",
            s->peer);
    if (s->state == ST_ACTIVE) {
        update_screen(s);
    }
}

static int first_t2(SESSION *s, unsigned int type2)
{
    unsigned char bit = (unsigned char) (1 << (type2 & 7));

    if (s->t2_logged[type2 >> 3] & bit) {
        return 0;
    }
    s->t2_logged[type2 >> 3] |= bit;
    return 1;
}

static void data_pdu(SESSION *s, INS *in)
{
    unsigned int len = in_left(in);
    unsigned int type2, compressed, action;
    const char *name;

    in_skip(in, 6 + 4 + 1 + 1 + 2);
    type2 = in_u8(in);
    compressed = in_u8(in);
    in_skip(in, 2);
    if (in->fail) {
        log_msg(LOG_WARN, "session %s: a Share Data PDU of %u bytes: too "
                "short for its header (rdp-1 5.4); ignored", s->peer, len);
        return;
    }
    name = type2_name(type2);
    if (compressed & 0x20) {
        log_msg(LOG_WARN, "session %s: a compressed %s PDU (type %u) from "
                "the client; ignored", s->peer, name ? name : "?", type2);
        return;
    }

    switch (type2) {
    case T2_SYNCHRONIZE:
        if (s->state == ST_FINALIZE) {
            send_synchronize(s);
            return;
        }
        break;
    case T2_CONTROL:
        action = in_u16le(in);
        if (s->state == ST_FINALIZE && action == CTRL_COOPERATE) {
            send_control(s, CTRL_COOPERATE, 0, 0);
            return;
        }
        if (s->state == ST_FINALIZE && action == CTRL_REQUEST) {
            send_control(s, CTRL_GRANTED, s->user_id, MCS_SERVER_USER);
            return;
        }
        log_msg(LOG_WARN, "session %s: Control action %u in state %s: "
                "ignored", s->peer, action, rdp_state_name(s->state));
        return;
    case T2_FONTLIST:
        if (s->state == ST_FINALIZE) {
            in_skip(in, 4);
            if (in_u16le(in) & FONTLIST_LAST) {
                send_font_map(s);
            }
            return;
        }
        break;
    case T2_INPUT:
        input_pdu(s, in);
        return;
    case T2_REFRESH_RECT:
        if (s->state == ST_ACTIVE && s->suppressed) {
            log_msg(LOG_DEBUG, "session %s: Refresh Rect while the output "
                    "is suppressed: ignored (rdp-2 5.1 rule 5)", s->peer);
            return;
        }
        if (s->state == ST_ACTIVE) {
            refresh_rect(s, in);
            return;
        }
        break;
    case T2_SUPPRESS_OUTPUT:
        suppress_output(s, in);
        return;
    case T2_SHUTDOWN_REQUEST:
        log_msg(LOG_INFO, "session %s: the client ends the session "
                "(Shutdown Request)", s->peer);
        mcs_send_dpum(s, MCS_RN_USER);
        session_end(s, 1, "the client ended it (Shutdown Request)");
        return;
    case T2_PERSISTENT_LIST:
        return;
    }
    if (name) {
        log_msg(LOG_DEBUG, "session %s: %s PDU (%u bytes) in state %s: "
                "ignored", s->peer, name, len, rdp_state_name(s->state));
    } else if (first_t2(s, type2)) {
        log_msg(LOG_WARN, "session %s: unknown Share Data PDU type %u (%u "
                "bytes): ignored (rdp-2 5.5). Logged once per type.",
                s->peer, type2, len);
    }
}

static void share_pdu(SESSION *s, INS *in)
{
    INS peek = *in;
    unsigned int type;

    in_skip(&peek, 2);
    type = in_u16le(&peek) & 0x000F;
    switch (type) {
    case SHARE_CONFIRM_ACTIVE:
        if (s->state == ST_WAIT_CONFIRM) {
            caps_confirm_active(s, in);
            return;
        }
        log_msg(LOG_WARN, "session %s: Confirm Active in state %s: ignored",
                s->peer, rdp_state_name(s->state));
        return;
    case SHARE_DATA:
        if (s->state == ST_FINALIZE || s->state == ST_ACTIVE ||
            (s->state == ST_WAIT_CONFIRM && s->generation > 1)) {
            data_pdu(s, in);
            return;
        }
        log_msg(LOG_WARN, "session %s: Share Data PDU (%u bytes) in state "
                "%s: ignored", s->peer, in_left(in),
                rdp_state_name(s->state));
        return;
    }
    log_msg(LOG_WARN, "session %s: Share PDU type %u (%u bytes): ignored",
            s->peer, type, in_left(in));
}

void share_pdus(SESSION *s, INS *in)
{
    unsigned int total;
    INS pdu;

    while (in_left(in) > 0 && s->ending == END_NO) {
        if (in_left(in) < 2) {
            log_msg(LOG_WARN, "session %s: 1 byte after the last Share PDU: "
                    "ignored", s->peer);
            return;
        }
        total = in_ptr(in)[0] | (in_ptr(in)[1] << 8);
        if (total == FLOW_MARKER) {
            in_skip(in, FLOW_PDU);
            log_msg(LOG_DEBUG, "session %s: flow PDU skipped", s->peer);
            continue;
        }
        if (total < 6 || total > in_left(in)) {
            log_msg(LOG_WARN, "session %s: a Share PDU with totalLength %u "
                    "in %u bytes: the rest is ignored (rdp-1 5.5)", s->peer,
                    total, in_left(in));
            return;
        }
        in_sub(in, &pdu, total);
        share_pdu(s, &pdu);
    }
}
