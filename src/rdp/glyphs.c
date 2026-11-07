/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <stdio.h>
#include <string.h>
#include "log.h"
#include "caps.h"
#include "orders.h"
#include "glyphs.h"

#define FILL_HEADER 8
#define FILL_GLYPH  10

void glyphs_reset(SESSION *s)
{
    GLYPH_STATE *g = &s->glyph;
    int c;

    for (c = 0; c < GLYPH_CACHES; c++) {
        memset(g->cache[c].place, 0, sizeof(g->cache[c].place));
        memset(g->cache[c].serial, 0, sizeof(g->cache[c].serial));
        memset(g->cache[c].used, 0, sizeof(g->cache[c].used));
        memset(g->cache[c].slot_of, 0, sizeof(g->cache[c].slot_of));
    }
}

void glyphs_caps(SESSION *s, char *text)
{
    GLYPH_STATE *g = &s->glyph;
    const unsigned char *p = s->caps.glyph;
    unsigned int entries, cell, n = 0;
    int c, len;

    g->level = -1;
    g->usable = 0;
    for (c = 0; c < GLYPH_CACHES; c++) {
        g->cache[c].entries = 0;
        g->cache[c].cell = 0;
    }
    if (!CAPS_HAVE(&s->caps, CAPS_GLYPH)) {
        strcpy(text, "no Glyph Cache capability set");
        return;
    }
    len = sprintf(text, "glyph caches (entries x bytes):");
    for (c = 0; c < GLYPH_CACHES; c++) {
        entries = p[4 * c] | (unsigned int) p[4 * c + 1] << 8;
        cell = p[4 * c + 2] | (unsigned int) p[4 * c + 3] << 8;
        len += sprintf(text + len, " %u x %u", entries, cell);
        if (entries > GLYPH_SLOTS) {
            entries = GLYPH_SLOTS;
        }
        if (cell >= 4 && entries > 0) {
            g->cache[c].entries = entries;
            g->cache[c].cell = cell;
            n++;
        }
    }
    g->level = p[44] | (int) p[45] << 8;
    g->usable = g->level >= 1 && n > 0;
    sprintf(text + len, "; GlyphSupportLevel %d", g->level);
}

static int refuse(GLYPH_STATE *gs, int why)
{
    gs->why[why]++;
    return -1;
}

static int victim(const GLYPH_CACHE *gc, unsigned long num)
{
    unsigned long age, oldest = 0;
    unsigned int i;
    int v = -1;

    for (i = 0; i < gc->entries; i++) {
        if (gc->place[i] == 0) {
            return (int) i;
        }
        age = num - gc->used[i];
        if (age != 0 && (v < 0 || age > oldest)) {
            oldest = age;
            v = (int) i;
        }
    }
    return v;
}

static int find(const GLYPH_CACHE *gc, const CORE_GLYPHREF *r)
{
    int sl = gc->slot_of[r->id] - 1;

    if (sl >= 0 && gc->place[sl] == r->id + 1 &&
        gc->serial[sl] == r->serial) {
        return sl;
    }
    return -1;
}

int glyphs_text(SESSION *s, const ORDER_SINK *k, const CORE_ORDER *o,
                unsigned long num, unsigned char *out, unsigned int room,
                unsigned int *count, int *done, int *type)
{
    GLYPH_STATE *gs = &s->glyph;
    GLYPH_CACHE *gc;
    CORE_GLYPHREF r[CORE_TEXT_MAX];
    const CORE_GLYPH *g[CORE_TEXT_MAX], *fill_g[CORE_TEXT_MAX];
    unsigned char fill_slot[CORE_TEXT_MAX];
    unsigned int need = 0, size, distinct = 0, fill = FILL_HEADER, len = 0;
    long bounds[4], *pb = NULL, l, t, rr, b;
    ORDER_TEXT txt;
    int i, j, c, best = -1, sl, nf = 0, all = 1, n = o->nglyph, rc;

    *count = 0;
    *done = 0;
    if (!gs->usable || !(k->allow & (ORD_A_FASTINDEX | ORD_A_GLYPHINDEX)) ||
        n < 1 || n > CORE_TEXT_MAX || n > ORD_TEXT_MAX) {
        return refuse(gs, GLYPHS_W_SETUP);
    }
    if (!s->core->TextGet(s->core_ctx, o, r)) {
        return refuse(gs, GLYPHS_W_RUN);
    }
    for (i = 0; i < n; i++) {
        if (r[i].id >= CORE_GLYPH_PLACES) {
            return refuse(gs, GLYPHS_W_GLYPH);
        }
        g[i] = s->core->GlyphGet(s->core_ctx, r[i].id, r[i].serial);
        if (g[i] == NULL) {
            return refuse(gs, GLYPHS_W_GLYPH);
        }
        if (g[i]->ox < -32768 || g[i]->ox > 32767 || g[i]->oy < -32768 ||
            g[i]->oy > 32767) {
            return refuse(gs, GLYPHS_W_RANGE);
        }
        size = ORD_GLYPH_BYTES(g[i]->cx, g[i]->cy);
        if (size > need) {
            need = size;
        }
        for (j = 0; j < i && r[j].id != r[i].id; j++) {
        }
        if (j == i) {
            distinct++;
        }
    }
    for (c = 0; c < GLYPH_CACHES; c++) {
        if (gs->cache[c].cell >= need && gs->cache[c].entries >= distinct &&
            (best < 0 || gs->cache[c].cell < gs->cache[best].cell)) {
            best = c;
        }
    }
    if (best < 0) {
        return refuse(gs, GLYPHS_W_CACHE);
    }
    gc = &gs->cache[best];

    memset(&txt, 0, sizeof(txt));
    txt.cache = best;
    txt.n = n;
    txt.y = o->y;
    txt.fg = o->color;
    txt.bg = o->bg;
    for (i = 0; i < n; i++) {
        txt.x[i] = r[i].x;
        l = (long) r[i].x + g[i]->ox;
        t = (long) o->y + g[i]->oy;
        rr = l + g[i]->cx;
        b = t + g[i]->cy;
        if (i == 0 || l < txt.bk[0]) {
            txt.bk[0] = l;
        }
        if (i == 0 || t < txt.bk[1]) {
            txt.bk[1] = t;
        }
        if (i == 0 || rr > txt.bk[2]) {
            txt.bk[2] = rr;
        }
        if (i == 0 || b > txt.bk[3]) {
            txt.bk[3] = b;
        }
    }
    l = o->op.left > o->rect.left ? o->op.left : o->rect.left;
    t = o->op.top > o->rect.top ? o->op.top : o->rect.top;
    rr = o->op.right < o->rect.right ? o->op.right : o->rect.right;
    b = o->op.bottom < o->rect.bottom ? o->op.bottom : o->rect.bottom;
    if (l < rr && t < b) {
        txt.op[0] = l;
        txt.op[1] = t;
        txt.op[2] = rr;
        txt.op[3] = b;
        for (i = 0; i < 2; i++) {
            if (txt.op[i] < txt.bk[i]) {
                txt.bk[i] = txt.op[i];
            }
            if (txt.op[2 + i] > txt.bk[2 + i]) {
                txt.bk[2 + i] = txt.op[2 + i];
            }
        }
    }
    if (txt.bk[0] < o->rect.left || txt.bk[1] < o->rect.top ||
        txt.bk[2] > o->rect.right || txt.bk[3] > o->rect.bottom) {
        bounds[0] = o->rect.left;
        bounds[1] = o->rect.top;
        bounds[2] = o->rect.right - 1;
        bounds[3] = o->rect.bottom - 1;
        pb = bounds;
    }
    if (order_text(&s->ord, k, &txt, pb, out, 0, type) < 0) {
        if (gs->why[GLYPHS_W_CODE] < 5) {
            log_msg(LOG_DEBUG, "session %s: text order not coded: %d "
                    "glyphs, x %ld - %ld, y %ld, bk (%ld,%ld)-(%ld,%ld), "
                    "op (%ld,%ld)-(%ld,%ld), rect (%ld,%ld)-(%ld,%ld)",
                    s->peer, n, txt.x[0], txt.x[n - 1], txt.y, txt.bk[0],
                    txt.bk[1], txt.bk[2], txt.bk[3], txt.op[0], txt.op[1],
                    txt.op[2], txt.op[3], (long) o->rect.left,
                    (long) o->rect.top, (long) o->rect.right,
                    (long) o->rect.bottom);
        }
        return refuse(gs, GLYPHS_W_CODE);
    }

    for (i = 0; i < n; i++) {
        sl = find(gc, &r[i]);
        if (sl >= 0) {
            gc->used[sl] = num;
        }
    }
    for (i = 0; i < n; i++) {
        sl = find(gc, &r[i]);
        if (sl >= 0) {
            gc->used[sl] = num;
            txt.slot[i] = (unsigned char) sl;
            continue;
        }
        size = FILL_GLYPH + ORD_GLYPH_BYTES(g[i]->cx, g[i]->cy);
        if (fill + size > room) {
            all = 0;
            break;
        }
        sl = victim(gc, num);
        if (sl < 0) {
            all = 0;
            break;
        }
        if (gc->place[sl] != 0 &&
            gc->slot_of[gc->place[sl] - 1] == (unsigned char) (sl + 1)) {
            gc->slot_of[gc->place[sl] - 1] = 0;
        }
        gc->place[sl] = (unsigned short) (r[i].id + 1);
        gc->serial[sl] = r[i].serial;
        gc->used[sl] = num;
        gc->slot_of[r[i].id] = (unsigned char) (sl + 1);
        txt.slot[i] = (unsigned char) sl;
        fill_slot[nf] = (unsigned char) sl;
        fill_g[nf] = g[i];
        nf++;
        fill += size;
    }
    if (nf > 0) {
        len = order_cache_glyph(best, nf, fill_slot, fill_g, out, room);
        gs->fills++;
        gs->glyphs += (unsigned long) nf;
        gs->fill_bytes += len;
        *count = 1;
    }
    if (!all) {
        return (int) len;
    }
    rc = order_text(&s->ord, k, &txt, pb, out + len, room - len, type);
    if (rc <= 0) {
        return (int) len;
    }
    *count += 1;
    *done = 1;
    return (int) len + rc;
}
