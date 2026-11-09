/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <stdio.h>
#include <string.h>
#include "log.h"
#include "stream.h"
#include "frame.h"
#include "session.h"
#include "x224.h"
#include "mcs.h"
#include "gcc.h"
#include "sec.h"
#include "lic.h"
#include "caps.h"
#include "share.h"
#include "update.h"
#include "bitmaps.h"
#include "pointer.h"
#include "rdp.h"

const char *rdp_state_name(int state)
{
    switch (state) {
    case ST_WAIT_CR:      return "WAIT_CR";
    case ST_WAIT_CI:      return "WAIT_CI";
    case ST_WAIT_DOMAIN:  return "WAIT_DOMAIN";
    case ST_WAIT_JOINS:   return "WAIT_JOINS";
    case ST_WAIT_CONFIRM: return "WAIT_CONFIRM";
    case ST_FINALIZE:     return "FINALIZE";
    case ST_ACTIVE:       return "ACTIVE";
    }
    return "?";
}

static const CORE_API *g_core;
static void           *g_core_ctx;

static void __cdecl ev_damage(void *fe, const RECT *rects, int count,
                              unsigned long at)
{
    SESSION *s = (SESSION *) fe;
    const RECT *r;
    RECT *d;
    int i;

    EnterCriticalSection(&s->lock);
    for (i = 0; i < count; i++) {
        r = &rects[i];
        if (r->left >= r->right || r->top >= r->bottom) {
            continue;
        }
        d = &s->ev_rects[s->ev_nrects > 0 ? s->ev_nrects - 1 : 0];
        if (s->ev_nrects == 0 ||
            (s->ev_nrects < SESS_EV_RECTS &&
             (r->left > d->right || r->right < d->left ||
              r->top > d->bottom || r->bottom < d->top))) {
            s->ev_at[s->ev_nrects] = at;
            s->ev_rects[s->ev_nrects++] = *r;
            continue;
        }
        if (r->left < d->left) {
            d->left = r->left;
        }
        if (r->top < d->top) {
            d->top = r->top;
        }
        if (r->right > d->right) {
            d->right = r->right;
        }
        if (r->bottom > d->bottom) {
            d->bottom = r->bottom;
        }
    }
    LeaveCriticalSection(&s->lock);
    session_wake(s);
}

static void ev_flag(SESSION *s, int flag)
{
    EnterCriticalSection(&s->lock);
    s->ev_flags |= flag;
    LeaveCriticalSection(&s->lock);
    session_wake(s);
}

static void __cdecl ev_frame(void *fe)
{
    ev_flag((SESSION *) fe, SESS_EV_FRAME);
}

static void __cdecl ev_orders(void *fe)
{
    ev_flag((SESSION *) fe, SESS_EV_ORDERS);
}

static void __cdecl ev_pointer_shape(void *fe)
{
    ev_flag((SESSION *) fe, SESS_EV_POINTER);
}

static void __cdecl ev_pointer_pos(void *fe, int x, int y)
{
    SESSION *s = (SESSION *) fe;

    EnterCriticalSection(&s->lock);
    s->ev_flags |= SESS_EV_POS;
    s->ev_x = x;
    s->ev_y = y;
    LeaveCriticalSection(&s->lock);
    session_wake(s);
}

static void __cdecl ev_stop(void *fe, int why)
{
    SESSION *s = (SESSION *) fe;

    EnterCriticalSection(&s->lock);
    s->ev_flags |= SESS_EV_STOP;
    s->ev_stop = why;
    LeaveCriticalSection(&s->lock);
    session_wake(s);
}

static void __cdecl ev_hold(void *fe, int on)
{
    SESSION *s = (SESSION *) fe;

    EnterCriticalSection(&s->lock);
    s->ev_flags &= ~(SESS_EV_HOLD | SESS_EV_SHOW);
    s->ev_flags |= on ? SESS_EV_HOLD : SESS_EV_SHOW;
    LeaveCriticalSection(&s->lock);
    session_wake(s);
}

static const FRONTEND_API rdp_events = {
    sizeof(FRONTEND_API), CORE_VERSION,
    ev_damage, ev_orders,
    ev_frame, ev_pointer_shape, ev_pointer_pos, ev_stop, ev_hold
};

int rdp_frame_depth(const CORE_FRAME *f)
{
    if (f->bpp == 16 && f->green_mask == 0x03E0) {
        return 15;
    }
    return (int) f->bpp;
}

int rdp_same_mode(const SESSION *s, const CORE_FRAME *f)
{
    return f->width == s->width && f->height == s->height &&
           rdp_frame_depth(f) == s->console_depth;
}

static void take_mode(SESSION *s, const CORE_FRAME *f)
{
    s->width = f->width;
    s->height = f->height;
    s->console_depth = rdp_frame_depth(f);
    if (s->state >= ST_WAIT_DOMAIN) {
        s->depth = gcc_session_depth(s, s->console_depth);
    }
}

static void reactivate(SESSION *s)
{
    CORE_FRAME f;

    s->mode_due = 0;
    memset(&f, 0, sizeof(f));
    f.cb = sizeof(f);
    if (!s->core->FrameLock(s->core_ctx, &f)) {
        session_end(s, 0, "the core has no screen");
        return;
    }
    s->core->FrameUnlock(s->core_ctx);
    if (rdp_same_mode(s, &f)) {
        update_screen(s);
        return;
    }
    if (!share_deactivate_all(s)) {
        return;
    }
    s->phase_ms = GetTickCount();
    update_stop(s);
    take_mode(s, &f);
    log_msg(LOG_INFO, "session %s: the console changed to %u x %u, %d bpp: "
            "reactivation at %d bpp (rdp-1 10)", s->peer, s->width,
            s->height, s->console_depth, s->depth);
    if (caps_demand_active(s)) {
        s->state = ST_WAIT_CONFIRM;
    }
}

static int g_compress = 1;
static int g_compress_max = 1;

void rdp_set_compression(int on, int max_type)
{
    g_compress = on ? 1 : 0;
    g_compress_max = max_type ? 1 : 0;
}

int rdp_compression(int *max_type)
{
    *max_type = g_compress_max;
    return g_compress;
}

#ifdef NTRDPS_TLS
static int g_allow_tls = 1;

void rdp_set_allow_tls(int on)
{
    g_allow_tls = on ? 1 : 0;
}

int rdp_allow_tls(void)
{
    return g_allow_tls;
}
#endif

void rdp_start(const CORE_API *core, void *core_ctx)
{
    g_core = core;
    g_core_ctx = core_ctx;
}

void rdp_init(SESSION *s)
{
    CORE_FRAME f;

    s->state = ST_WAIT_CR;
    s->phase_ms = GetTickCount();
    InitializeCriticalSection(&s->lock);
    s->core = g_core;
    s->core_ctx = g_core_ctx;
    s->core_session = g_core->SessionOpen(g_core_ctx, &rdp_events, s,
                                          s->peer, 0);
    if (s->core_session == NULL) {
        session_end(s, 0, "the core refused the session");
        return;
    }
    memset(&f, 0, sizeof(f));
    f.cb = sizeof(f);
    if (!s->core->FrameLock(s->core_ctx, &f)) {
        session_end(s, 0, "the core has no screen");
        return;
    }
    s->console_depth = rdp_frame_depth(&f);
    s->width = f.width;
    s->height = f.height;
    s->core->FrameUnlock(s->core_ctx);
}

void rdp_done(SESSION *s)
{
    update_done(s);
    pointer_done(s);
    share_done(s);
    if (s->core_session != NULL) {
        s->core->SessionClose(s->core_ctx, s->core_session);
        s->core_session = NULL;
    }
    DeleteCriticalSection(&s->lock);
}

void rdp_packet(SESSION *s, int kind, unsigned char *p, unsigned int len)
{
    INS mcs;

    if (kind == FRAME_TPKT && s->state < ST_WAIT_CONFIRM) {
        sec_hide_password(p, len);
    }
    log_hex("in", p, len);
    if (kind == FRAME_FASTPATH) {
        s->fastpath++;
        if (s->fastpath == 1) {
            log_msg(LOG_WARN, "session %s: fast-path input (%u bytes) "
                    "skipped; more are only counted", s->peer, len);
        }
        return;
    }
    if (s->state == ST_WAIT_CR) {
        x224_connect(s, p, len);
        return;
    }
    if (!x224_data(s, p, len, &mcs)) {
        return;
    }
    mcs_pdu(s, &mcs);
}

static void stop_session(SESSION *s, int why)
{
    switch (why) {
    case CORE_STOP_REPLACED:
        rdp_end(s, ERRINFO_OTHER_CONNECTION,
                "a new client has the console now");
        break;
    case CORE_STOP_BUSY:
        rdp_end(s, ERRINFO_DENIED, "refused: another client has the "
                "console, or waits for the console user");
        break;
    default:
        rdp_end(s, ERRINFO_DENIED, "the console prompt did not allow it");
        break;
    }
}

void rdp_data(SESSION *s, INS *in)
{
    int why;

    if (s->state == ST_WAIT_JOINS) {
        if (!sec_client_info(s, in)) {
            return;
        }
        why = s->core_session != NULL
            ? s->core->SessionReady(s->core_ctx, s->core_session)
            : CORE_STOP_BUSY;
        if (!lic_send(s)) {
            return;
        }
        if (why != 0) {
            stop_session(s, why);
        } else if (caps_demand_active(s)) {
            s->state = ST_WAIT_CONFIRM;
        }
        return;
    }
    if (sec_is_license(in)) {
        log_msg(LOG_WARN, "session %s: a license packet from the client "
                "(%u bytes): dropped (rdp-1 5.2)", s->peer, in_left(in));
        return;
    }
    share_pdus(s, in);
}

void rdp_take_damage(SESSION *s)
{
    RECT d[SESS_EV_RECTS];
    unsigned long at[SESS_EV_RECTS];
    int n, i;

    EnterCriticalSection(&s->lock);
    n = s->ev_nrects;
    memcpy(d, s->ev_rects, n * sizeof(d[0]));
    memcpy(at, s->ev_at, n * sizeof(at[0]));
    s->ev_nrects = 0;
    LeaveCriticalSection(&s->lock);
    for (i = 0; i < n; i++) {
        update_damage(s, &d[i], at[i]);
    }
}

static void take_events(SESSION *s)
{
    CORE_FRAME f;
    int flags, x, y, why;

    EnterCriticalSection(&s->lock);
    flags = s->ev_flags;
    x = s->ev_x;
    y = s->ev_y;
    why = s->ev_stop;
    s->ev_flags = 0;
    LeaveCriticalSection(&s->lock);

    if (flags & SESS_EV_STOP) {
        stop_session(s, why);
        return;
    }
    if (flags & SESS_EV_SHOW) {
        s->held = 0;
        flags |= SESS_EV_FRAME | SESS_EV_POINTER;
    } else if (flags & SESS_EV_HOLD) {
        s->held = 1;
    }
    if (flags & SESS_EV_FRAME) {
        memset(&f, 0, sizeof(f));
        f.cb = sizeof(f);
        if (!s->core->FrameLock(s->core_ctx, &f)) {
            session_end(s, 0, "the core has no screen");
            return;
        }
        s->core->FrameUnlock(s->core_ctx);
        if (rdp_same_mode(s, &f)) {
            if (s->depth == 8 && s->console_depth == 8) {
                s->upd_palette = 1;
                bitmaps_reset(s);
            }
            update_area(s, 0, 0, (long) s->width, (long) s->height);
        } else if (s->state < ST_WAIT_CONFIRM) {
            take_mode(s, &f);
            log_msg(LOG_INFO, "session %s: the console changed to %u x %u, "
                    "%d bpp before the Demand Active", s->peer, s->width,
                    s->height, s->console_depth);
        } else {
            s->mode_due = 1;
        }
    }
    rdp_take_damage(s);
    if (flags & SESS_EV_ORDERS) {
        s->ord_due = 1;
    }
    if (flags & SESS_EV_POINTER) {
        pointer_changed(s);
    }
    if (flags & SESS_EV_POS) {
        pointer_moved(s, x, y);
    }
}

void rdp_pump(SESSION *s)
{
    take_events(s);
    if (s->mode_due && s->state == ST_ACTIVE && s->ending == END_NO) {
        reactivate(s);
    }
    if (s->held) {
        return;
    }
    pointer_pump(s);
    update_pump(s);
}

void rdp_end(SESSION *s, unsigned long errinfo, const char *why)
{
    if (s->ending != END_NO) {
        return;
    }
    if (errinfo != 0 && s->licensed && (s->early_flags & 0x0001)) {
        share_send_error_info(s, errinfo);
    }
    if (s->state >= ST_WAIT_DOMAIN) {
        mcs_send_dpum(s, MCS_RN_PROVIDER);
    }
    session_end(s, 1, "%s", why);
}

void rdp_service_stop(SESSION *s)
{
    rdp_end(s, ERRINFO_RPC_INITIATED, "ntrdps stops");
}

void rdp_check_time(SESSION *s, DWORD now)
{
    char why[80];

    if (s->ending != END_NO || s->state == ST_ACTIVE ||
        now - s->phase_ms <= RDP_HANDSHAKE_MS) {
        return;
    }
    sprintf(why, "no complete handshake within %d s (state %s)",
            RDP_HANDSHAKE_MS / 1000, rdp_state_name(s->state));
    rdp_end(s, 0, why);
}
