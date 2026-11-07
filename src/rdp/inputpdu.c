/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <string.h>
#include "log.h"
#include "stream.h"
#include "session.h"
#include "inputpdu.h"

#define EV_SYNC     0x0000
#define EV_SCANCODE 0x0004
#define EV_MOUSE    0x8001
#define EV_MOUSEX   0x8002

#define KBD_EXTENDED  0x0100
#define KBD_EXTENDED1 0x0200
#define KBD_RELEASE   0x8000

#define PTR_ROTATION  0x01FF
#define PTR_NEGATIVE  0x0100
#define PTR_WHEEL     0x0200
#define PTR_MOVE      0x0800
#define PTR_BUTTON1   0x1000
#define PTR_BUTTON2   0x2000
#define PTR_BUTTON3   0x4000
#define PTR_BUTTONS   0x7000
#define PTR_DOWN      0x8000

static unsigned int get16(const unsigned char *p)
{
    return p[0] | (p[1] << 8);
}

static int clamp(unsigned int v, unsigned int size)
{
    long x = v >= 0x8000 ? (long) v - 0x10000L : (long) v;

    if (x >= (long) size) {
        x = (long) size - 1;
    }
    return x < 0 ? 0 : (int) x;
}

void input_decode(const unsigned char *ev, unsigned int w, unsigned int h,
                  INPUT_EVENT *e)
{
    unsigned int f = get16(ev + 6);

    memset(e, 0, sizeof(*e));
    e->type = get16(ev + 4);
    e->flags = f;
    switch (e->type) {
    case EV_SYNC:
        e->kind = IN_SYNC;
        e->code = get16(ev + 8) | ((unsigned long) get16(ev + 10) << 16);
        return;
    case EV_SCANCODE:
        e->code = get16(ev + 8);
        if (e->code >= 0x80) {
            e->kind = IN_SKIP;
            e->why = "a keyCode of 0x80 or more (rdp-2 2.4 rule 3)";
            return;
        }
        e->kind = IN_KEY;
        e->ext = (f & KBD_EXTENDED1) ? 0xE1 : (f & KBD_EXTENDED) ? 0xE0 : 0;
        e->up = (f & KBD_RELEASE) != 0;
        return;
    case EV_MOUSE:
        if (f & PTR_WHEEL) {
            if (f & PTR_BUTTONS) {
                e->kind = IN_SKIP;
                e->why = "a wheel event with a button bit (rdp-2 2.5 step 1)";
                return;
            }
            e->kind = IN_WHEEL;
            e->wheel = (int) (f & PTR_ROTATION);
            if (f & PTR_NEGATIVE) {
                e->wheel -= 512;
            }
            return;
        }
        e->x = clamp(get16(ev + 8), w);
        e->y = clamp(get16(ev + 10), h);
        if (f & PTR_MOVE) {
            e->kind = IN_MOVE;
            return;
        }
        switch (f & PTR_BUTTONS) {
        case PTR_BUTTON1:
            e->code = 1;
            break;
        case PTR_BUTTON2:
            e->code = 2;
            break;
        case PTR_BUTTON3:
            e->code = 3;
            break;
        default:
            e->kind = IN_SKIP;
            e->why = "no move, no wheel and not one button (rdp-2 2.5 "
                     "step 5)";
            return;
        }
        e->kind = IN_BUTTON;
        e->up = (f & PTR_DOWN) == 0;
        return;
    }
    e->kind = IN_SKIP;
    e->why = e->type == EV_MOUSEX
             ? "MOUSEX: NT has no X buttons (rdp-2 2.6)"
             : "a type we do not use (rdp-2 2.2)";
}

static int first_skip(SESSION *s, unsigned int type)
{
    unsigned int i;

    for (i = 0; i < s->ev_logged_n; i++) {
        if (s->ev_logged[i] == type) {
            return 0;
        }
    }
    if (s->ev_logged_n == sizeof(s->ev_logged) / sizeof(s->ev_logged[0])) {
        return 0;
    }
    s->ev_logged[s->ev_logged_n++] = type;
    return 1;
}

static void log_event(SESSION *s, const INPUT_EVENT *e)
{
    static const char *const buttons[4] = { "", "left", "right", "middle" };

    switch (e->kind) {
    case IN_SYNC:
        log_msg(LOG_DEBUG, "session %s: input: sync, toggleFlags 0x%02lx "
                "(Scroll Lock %s, Num Lock %s, Caps Lock %s)", s->peer,
                e->code, (e->code & 1) ? "on" : "off",
                (e->code & 2) ? "on" : "off", (e->code & 4) ? "on" : "off");
        return;
    case IN_KEY:
        log_msg(LOG_DEBUG, "session %s: input: key %s0x%02lx %s "
                "(keyboardFlags 0x%04x)", s->peer,
                e->ext == 0xE1 ? "E1 " : e->ext == 0xE0 ? "E0 " : "",
                e->code, e->up ? "up" : "down", e->flags);
        return;
    case IN_MOVE:
        log_msg(LOG_DEBUG, "session %s: input: mouse move to (%d, %d)",
                s->peer, e->x, e->y);
        return;
    case IN_BUTTON:
        log_msg(LOG_DEBUG, "session %s: input: mouse %s button %s at "
                "(%d, %d)", s->peer, buttons[e->code], e->up ? "up" : "down",
                e->x, e->y);
        return;
    case IN_WHEEL:
        log_msg(LOG_DEBUG, "session %s: input: mouse wheel %+d", s->peer,
                e->wheel);
        return;
    }
    if (first_skip(s, e->type)) {
        log_msg(LOG_INFO, "session %s: input event type 0x%04x (flags "
                "0x%04x) skipped: %s. Logged once per type.", s->peer,
                e->type, e->flags, e->why);
    }
}

static void give(SESSION *s, const INPUT_EVENT *e)
{
    const CORE_API *c = s->core;
    unsigned int bit, locks;

    switch (e->kind) {
    case IN_SYNC:
        locks = ((e->code & 1) ? CORE_LOCK_SCROLL : 0) |
                ((e->code & 2) ? CORE_LOCK_NUM : 0) |
                ((e->code & 4) ? CORE_LOCK_CAPS : 0);
        c->KeyLocks(s->core_ctx, s->core_session, locks);
        return;
    case IN_KEY:
        c->Key(s->core_ctx, s->core_session, (unsigned int) e->code, e->ext,
               !e->up);
        return;
    case IN_MOVE:
        break;
    case IN_BUTTON:
        bit = e->code == 1 ? CORE_BUTTON_LEFT
            : e->code == 2 ? CORE_BUTTON_RIGHT : CORE_BUTTON_MIDDLE;
        if (e->up) {
            s->buttons &= ~bit;
        } else {
            s->buttons |= bit;
        }
        break;
    case IN_WHEEL:
        c->Mouse(s->core_ctx, s->core_session, s->mouse_x, s->mouse_y,
                 s->buttons, e->wheel);
        return;
    default:
        return;
    }
    s->mouse_x = e->x;
    s->mouse_y = e->y;
    s->mouse_seen = 1;
    s->mouse_ms = GetTickCount();
    c->Mouse(s->core_ctx, s->core_session, e->x, e->y, s->buttons, 0);
}

void input_pdu(SESSION *s, INS *in)
{
    unsigned int len = in_left(in);
    unsigned int n, room, i;
    INPUT_EVENT e;

    if (len < 4) {
        log_msg(LOG_WARN, "session %s: an Input PDU with a body of %u "
                "bytes: dropped (rdp-2 2.7 rule 2)", s->peer, len);
        return;
    }
    n = in_u16le(in);
    in_skip(in, 2);
    room = in_left(in) / INPUT_EVENT_SIZE;
    if (room < n) {
        log_msg(LOG_WARN, "session %s: short Input PDU: %u events, room "
                "for %u; only those are used (rdp-2 2.7 rule 4)", s->peer,
                n, room);
        n = room;
    }
    for (i = 0; i < n; i++) {
        input_decode(in_ptr(in), s->width, s->height, &e);
        in_skip(in, INPUT_EVENT_SIZE);
        s->in_events++;
        log_event(s, &e);
        give(s, &e);
    }
}
