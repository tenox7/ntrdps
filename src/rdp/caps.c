/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <stdio.h>
#include <string.h>
#include "log.h"
#include "stream.h"
#include "session.h"
#include "mcs.h"
#include "share.h"
#include "caps.h"
#include "glyphs.h"
#include "bitmaps.h"

static const unsigned char order_list[] = {
    0, 1, 2, 3, 8, 10, 11, 18, 19, 27
};

static void set_general(OUTS *o)
{
    out_u16le(o, CAPS_GENERAL);
    out_u16le(o, 24);
    out_u16le(o, 1);
    out_u16le(o, 3);
    out_u16le(o, 0x0200);
    out_u16le(o, 0);
    out_u16le(o, 0);
    out_u16le(o, 0x0400);
    out_u16le(o, 0);
    out_u16le(o, 0);
    out_u16le(o, 0);
    out_u8(o, 1);
    out_u8(o, 1);
}

static void set_bitmap(OUTS *o, const SESSION *s)
{
    out_u16le(o, CAPS_BITMAP);
    out_u16le(o, 28);
    out_u16le(o, s->depth);
    out_u16le(o, 1);
    out_u16le(o, 1);
    out_u16le(o, 1);
    out_u16le(o, (s->width + 3) & ~3U);
    out_u16le(o, s->height);
    out_u16le(o, 0);
    out_u16le(o, 1);
    out_u16le(o, 1);
    out_u8(o, 0);
    out_u8(o, 0);
    out_u16le(o, 1);
    out_u16le(o, 0);
}

static void set_order(OUTS *o)
{
    unsigned char support[32];
    unsigned int i;

    memset(support, 0, sizeof(support));
    for (i = 0; i < sizeof(order_list); i++) {
        support[order_list[i]] = 1;
    }
    out_u16le(o, CAPS_ORDER);
    out_u16le(o, 88);
    out_zero(o, 16);
    out_u32le(o, 1000000UL);
    out_u16le(o, 1);
    out_u16le(o, 20);
    out_u16le(o, 0);
    out_u16le(o, 1);
    out_u16le(o, 0);
    out_u16le(o, 0x0022);
    out_bytes(o, support, sizeof(support));
    out_u16le(o, 0x06A1);
    out_u16le(o, 0);
    out_u32le(o, 1000000UL);
    out_u32le(o, 1000000UL);
    out_u16le(o, 1);
    out_u16le(o, 0);
    out_u16le(o, 0);
    out_u16le(o, 0);
}

static void set_others(OUTS *o)
{
    out_u16le(o, CAPS_POINTER);
    out_u16le(o, 10);
    out_u16le(o, 1);
    out_u16le(o, 25);
    out_u16le(o, 25);

    out_u16le(o, CAPS_INPUT);
    out_u16le(o, 88);
    out_u16le(o, 0x0001);
    out_zero(o, 2 + 16 + 64);

    out_u16le(o, CAPS_SHARE);
    out_u16le(o, 8);
    out_u16le(o, MCS_SERVER_USER);
    out_u16le(o, 0);

    out_u16le(o, CAPS_COLCACHE);
    out_u16le(o, 8);
    out_u16le(o, 6);
    out_u16le(o, 0);

    out_u16le(o, CAPS_FONT);
    out_u16le(o, 8);
    out_u16le(o, 0x0001);
    out_u16le(o, 0);
}

int caps_demand_active(SESSION *s)
{
    unsigned char buf[400];
    OUTS o;
    unsigned int comb;

    order_reset(&s->ord);
    glyphs_reset(s);
    bitmaps_reset(s);
    share_restart_compression(s);
    s->ord_pdu.count = 0;
    s->ord_pdu.len = 0;
    s->generation++;
    s->share_id = MCS_SERVER_USER | ((unsigned long) s->generation << 16);

    mcs_sdin_begin(&o, buf, sizeof(buf));
    out_u16le(&o, 0);
    out_u16le(&o, SHARE_VERSION | SHARE_DEMAND_ACTIVE);
    out_u16le(&o, MCS_SERVER_USER);
    out_u32le(&o, s->share_id);
    out_u16le(&o, 4);
    comb = out_reserve(&o, 2);
    out_bytes(&o, "RDP", 4);
    out_u16le(&o, 8);
    out_u16le(&o, 0);
    set_general(&o);
    set_bitmap(&o, s);
    set_order(&o);
    set_others(&o);
    out_put_u16le(&o, comb, out_len(&o) - (comb + 2 + 4));
    out_u32le(&o, 0);
    out_put_u16le(&o, MCS_SDIN_HEADER, out_len(&o) - MCS_SDIN_HEADER);
    if (!mcs_sdin_end(s, &o)) {
        return 0;
    }
    log_msg(LOG_INFO, "session %s: Demand Active: share 0x%08lx, %u x %u, "
            "%d bpp", s->peer, s->share_id, (s->width + 3) & ~3U, s->height,
            s->depth);
    return 1;
}

static void read_set(CLIENT_CAPS *c, unsigned int type, INS *b)
{
    unsigned int i;

    if (type < 32) {
        c->have |= 1UL << type;
    }
    switch (type) {
    case CAPS_GENERAL:
        in_skip(b, 10);
        c->extra_flags = in_u16le(b);
        break;
    case CAPS_BITMAP:
        c->ask_bpp = in_u16le(b);
        in_skip(b, 6);
        c->ask_width = in_u16le(b);
        c->ask_height = in_u16le(b);
        in_skip(b, 4);
        c->bitmap_compression = in_u16le(b);
        break;
    case CAPS_ORDER:
        in_skip(b, 30);
        c->order_flags = in_u16le(b);
        in_bytes(b, c->order_support, 32);
        c->text_flags = in_u16le(b);
        in_skip(b, 6);
        c->desktop_save_size = in_u32le(b);
        break;
    case CAPS_BMCACHE:
        in_skip(b, 24);
        for (i = 0; i < 3; i++) {
            c->bmc_entries[i] = in_u16le(b);
            c->bmc_cell_size[i] = in_u16le(b);
        }
        break;
    case CAPS_POINTER:
        c->color_pointer = in_u16le(b);
        c->color_pointer_cache = in_u16le(b);
        if (in_left(b) >= 2) {
            c->pointer_cache = in_u16le(b);
            c->pointer_new = 1;
        }
        break;
    case CAPS_COLCACHE:
        c->color_table_cache = in_u16le(b);
        break;
    case CAPS_INPUT:
        c->input_flags = in_u16le(b);
        in_skip(b, 2);
        c->input_kbd_layout = in_u32le(b);
        break;
    case CAPS_BRUSH:
        c->brush_level = in_u32le(b);
        break;
    case CAPS_GLYPH:
        in_bytes(b, c->glyph, sizeof(c->glyph));
        break;
    case CAPS_OFFSCREEN:
        in_bytes(b, c->offscreen, sizeof(c->offscreen));
        break;
    case CAPS_BMCACHE2:
        in_bytes(b, c->bmc2, sizeof(c->bmc2));
        break;
    }
}

void caps_confirm_active(SESSION *s, INS *in)
{
    CLIENT_CAPS *c = &s->caps;
    unsigned long share;
    unsigned int src_len, comb, n, i, type, len, orders;
    unsigned char src16[16];
    char src[17], types[160];
    INS sets, b;

    in_skip(in, 6);
    share = in_u32le(in);
    in_skip(in, 2);
    src_len = in_u16le(in);
    comb = in_u16le(in);
    memset(src16, 0, sizeof(src16));
    in_bytes(in, src16, src_len < 16 ? src_len : 16);
    in_skip(in, src_len < 16 ? 0 : src_len - 16);
    for (i = 0; i < 16 && src16[i]; i++) {
        src[i] = (src16[i] >= 0x20 && src16[i] < 0x7F) ? (char) src16[i] : '?';
    }
    src[i] = '\0';
    in_sub(in, &sets, comb);
    n = in_u16le(&sets);
    in_skip(&sets, 2);
    if (in->fail || sets.fail) {
        log_msg(LOG_WARN, "session %s: Confirm Active is shorter than its "
                "lengths say; no capability sets read", s->peer);
        n = 0;
    }
    if (share != s->share_id) {
        log_msg(LOG_WARN, "session %s: Confirm Active for share 0x%08lx; "
                "ours is 0x%08lx (rdp-1 8.4: accepted)", s->peer, share,
                s->share_id);
    }

    memset(c, 0, sizeof(*c));
    c->freerdp = strcmp(src, "FREERDP") == 0;
    types[0] = '\0';
    for (i = 0; i < n; i++) {
        type = in_u16le(&sets);
        len = in_u16le(&sets);
        if (sets.fail || len < 4 || len - 4 > in_left(&sets)) {
            log_msg(LOG_WARN, "session %s: Confirm Active capability set "
                    "%u of %u has a bad length; the rest is skipped",
                    s->peer, i + 1, n);
            break;
        }
        in_sub(&sets, &b, len - 4);
        read_set(c, type, &b);
        if (strlen(types) < sizeof(types) - 8) {
            sprintf(types + strlen(types), "%s%u", i ? " " : "", type);
        }
    }
    for (orders = 0, i = 0; i < 32; i++) {
        orders += c->order_support[i] != 0;
    }
    log_msg(LOG_INFO, "session %s: Confirm Active from \"%s\": %u sets "
            "(types %s); the client asks %u x %u, %u bpp (log only, rdp-1 "
            "8.4 rule 4)", s->peer, src, n, types, c->ask_width,
            c->ask_height, c->ask_bpp);
    log_msg(LOG_INFO, "session %s: client caps: extraFlags 0x%04x, bitmap "
            "compression %u, %u orders (orderFlags 0x%04x), color pointer %u "
            "(caches %u / %u), color tables %u, inputFlags 0x%04x, keyboard "
            "0x%08lx", s->peer, c->extra_flags, c->bitmap_compression, orders,
            c->order_flags, c->color_pointer, c->color_pointer_cache,
            c->pointer_cache, c->color_table_cache, c->input_flags,
            c->input_kbd_layout);
    s->state = ST_FINALIZE;
}
