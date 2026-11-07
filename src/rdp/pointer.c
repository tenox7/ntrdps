/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <string.h>
#include "log.h"
#include "stream.h"
#include "session.h"
#include "mcs.h"
#include "share.h"
#include "update.h"
#include "pointer.h"

#define MSG_SYSTEM   1
#define MSG_POSITION 3
#define MSG_COLOR    6
#define MSG_CACHED   7
#define MSG_POINTER  8

#define SYSPTR_NULL    0x00000000UL
#define SYSPTR_DEFAULT 0x00007F00UL

#define SIDE        CORE_POINTER_MAX
#define AND_BYTES   (SIDE / 8 * SIDE)
#define XOR1_BYTES  (SIDE / 8 * SIDE)
#define XOR24_BYTES (SIDE * 3 * SIDE)

#define PDU_MAX (MCS_SDIN_HEADER + SHARE_DATA_HEADER + 20 + XOR24_BYTES + \
                 AND_BYTES)

void pointer_reset(SESSION *s)
{
    POINTER_STATE *t = &s->ptr;
    unsigned int n = s->caps.pointer_new ? s->caps.pointer_cache
                                         : s->caps.color_pointer_cache;

    t->slots = n == 0 ? 1 : n < POINTER_SLOTS ? n : POINTER_SLOTS;
    t->filled = 0;
    t->clock = 0;
    t->shown = PTR_SHOWN_NONE;
    t->shape_due = 1;
    t->pos_due = 0;
    log_msg(LOG_DEBUG, "session %s: pointer: %u cache slots (the client "
            "has %u), message 8 %s", s->peer, t->slots, n,
            s->caps.pointer_new ? "allowed" : "not allowed");
}

void pointer_changed(SESSION *s)
{
    s->ptr.shape_due = 1;
}

void pointer_moved(SESSION *s, int x, int y)
{
    POINTER_STATE *t = &s->ptr;

    if (s->mouse_seen &&
        ((x == s->mouse_x && y == s->mouse_y) ||
         GetTickCount() - s->mouse_ms < POINTER_ECHO_MS)) {
        t->pos_due = 0;
        return;
    }
    t->pos_due = 1;
    t->pos_x = x;
    t->pos_y = y;
}

static void send_system(SESSION *s, unsigned long kind, int shown)
{
    unsigned char buf[64];
    OUTS o;

    if (s->ptr.shown == shown) {
        return;
    }
    share_begin(s, &o, buf, sizeof(buf));
    out_u16le(&o, MSG_SYSTEM);
    out_u16le(&o, 0);
    out_u32le(&o, kind);
    if (share_end(s, &o, T2_POINTER)) {
        s->ptr.shown = shown;
        s->ptr.sent_system++;
        log_msg(LOG_DEBUG, "session %s: pointer: %s", s->peer,
                kind == SYSPTR_NULL ? "hidden" : "the default arrow");
    }
}

static void send_position(SESSION *s)
{
    unsigned char buf[64];
    OUTS o;
    int x = s->ptr.pos_x, y = s->ptr.pos_y;

    x = x < 0 ? 0 : x >= (int) s->width ? (int) s->width - 1 : x;
    y = y < 0 ? 0 : y >= (int) s->height ? (int) s->height - 1 : y;
    share_begin(s, &o, buf, sizeof(buf));
    out_u16le(&o, MSG_POSITION);
    out_u16le(&o, 0);
    out_u16le(&o, (unsigned int) x);
    out_u16le(&o, (unsigned int) y);
    if (share_end(s, &o, T2_POINTER)) {
        s->ptr.sent_pos++;
    }
}

static void send_cached(SESSION *s, unsigned int i)
{
    unsigned char buf[64];
    OUTS o;

    share_begin(s, &o, buf, sizeof(buf));
    out_u16le(&o, MSG_CACHED);
    out_u16le(&o, 0);
    out_u16le(&o, i);
    if (share_end(s, &o, T2_POINTER)) {
        s->ptr.shown = PTR_SHOWN_SLOT;
        s->ptr.shown_slot = i;
        s->ptr.sent_cached++;
        log_msg(LOG_DEBUG, "session %s: pointer: slot %u again", s->peer,
                i);
    }
}

static int is_mono(const CORE_POINTER *p)
{
    const unsigned char *c = &p->color[0][0];
    unsigned int i;

    for (i = 0; i < sizeof(p->color); i += 3) {
        if ((c[i] != 0 || c[i + 1] != 0 || c[i + 2] != 0) &&
            (c[i] != 0xFF || c[i + 1] != 0xFF || c[i + 2] != 0xFF)) {
            return 0;
        }
    }
    return 1;
}

static void send_new(SESSION *s, unsigned int i, const CORE_POINTER *p)
{
    unsigned char buf[PDU_MAX];
    const unsigned char *c;
    unsigned int b;
    int mono = s->caps.pointer_new && is_mono(p);
    int x, y;
    OUTS o;

    share_begin(s, &o, buf, sizeof(buf));
    out_u16le(&o, mono ? MSG_POINTER : MSG_COLOR);
    out_u16le(&o, 0);
    if (mono) {
        out_u16le(&o, 1);
    }
    out_u16le(&o, i);
    out_u16le(&o, p->hot_x);
    out_u16le(&o, p->hot_y);
    out_u16le(&o, SIDE);
    out_u16le(&o, SIDE);
    out_u16le(&o, AND_BYTES);
    out_u16le(&o, mono ? XOR1_BYTES : XOR24_BYTES);
    if (mono) {
        for (y = 0; y < SIDE; y++) {
            for (x = 0; x < SIDE; x += 8) {
                c = &p->color[y][x * 3];
                b = (c[0] ? 0x80 : 0) | (c[3] ? 0x40 : 0) |
                    (c[6] ? 0x20 : 0) | (c[9] ? 0x10 : 0) |
                    (c[12] ? 0x08 : 0) | (c[15] ? 0x04 : 0) |
                    (c[18] ? 0x02 : 0) | (c[21] ? 0x01 : 0);
                out_u8(&o, b);
            }
        }
        for (y = 0; y < SIDE; y++) {
            out_bytes(&o, p->and_mask[y], SIDE / 8);
        }
    } else {
        for (y = SIDE - 1; y >= 0; y--) {
            c = p->color[y];
            for (x = 0; x < SIDE; x++, c += 3) {
                out_u8(&o, c[2]);
                out_u8(&o, c[1]);
                out_u8(&o, c[0]);
            }
        }
        for (y = SIDE - 1; y >= 0; y--) {
            out_bytes(&o, p->and_mask[y], SIDE / 8);
        }
    }
    if (share_end(s, &o, T2_POINTER)) {
        s->ptr.shown = PTR_SHOWN_SLOT;
        s->ptr.shown_slot = i;
        s->ptr.sent_new++;
        log_msg(LOG_DEBUG, "session %s: pointer: a new shape into slot %u: "
                "message %s, hot spot (%u, %u)", s->peer, i,
                mono ? "8, xorBpp 1" : "6", p->hot_x, p->hot_y);
    }
}

static int same(const POINTER_SHAPE *k, const CORE_POINTER *p)
{
    return k->hot_x == p->hot_x && k->hot_y == p->hot_y &&
           memcmp(k->and_mask, p->and_mask, sizeof(k->and_mask)) == 0 &&
           memcmp(k->color, p->color, sizeof(k->color)) == 0;
}

static void send_shape(SESSION *s)
{
    POINTER_STATE *t = &s->ptr;
    POINTER_SHAPE *k;
    CORE_POINTER p;
    unsigned int i, j;

    memset(&p, 0, sizeof(p));
    p.cb = sizeof(p);
    if (!s->core->PointerGet(s->core_ctx, &p) || p.serial == 0) {
        send_system(s, SYSPTR_DEFAULT, PTR_SHOWN_ARROW);
        return;
    }
    if (p.width == 0 || p.height == 0) {
        send_system(s, SYSPTR_NULL, PTR_SHOWN_HIDDEN);
        return;
    }
    for (i = 0; i < t->filled && !same(&t->slot[i], &p); i++) {
    }
    if (i < t->filled) {
        t->used[i] = ++t->clock;
        if (t->shown != PTR_SHOWN_SLOT || t->shown_slot != i) {
            send_cached(s, i);
        }
        return;
    }
    if (t->filled < t->slots) {
        i = t->filled++;
    } else {
        for (i = 0, j = 1; j < t->slots; j++) {
            if (t->used[j] < t->used[i]) {
                i = j;
            }
        }
    }
    k = &t->slot[i];
    k->hot_x = p.hot_x;
    k->hot_y = p.hot_y;
    memcpy(k->and_mask, p.and_mask, sizeof(k->and_mask));
    memcpy(k->color, p.color, sizeof(k->color));
    t->used[i] = ++t->clock;
    send_new(s, i, &p);
}

void pointer_pump(SESSION *s)
{
    POINTER_STATE *t = &s->ptr;

    if (s->state != ST_ACTIVE || s->ending != END_NO || s->suppressed ||
        t->slots == 0) {
        return;
    }
    if (t->shape_due && SESSION_QUEUED(s) < UPDATE_QUEUE_MAX) {
        t->shape_due = 0;
        send_shape(s);
    }
    if (t->pos_due && SESSION_QUEUED(s) < UPDATE_QUEUE_MAX) {
        t->pos_due = 0;
        if (t->shown != PTR_SHOWN_HIDDEN) {
            send_position(s);
        }
    }
}

void pointer_done(SESSION *s)
{
    POINTER_STATE *t = &s->ptr;

    if (t->sent_new + t->sent_cached + t->sent_system + t->sent_pos == 0) {
        return;
    }
    log_msg(LOG_INFO, "session %s: pointer: %lu new shapes, %lu from the "
            "cache, %lu system, %lu positions", s->peer, t->sent_new,
            t->sent_cached, t->sent_system, t->sent_pos);
}
