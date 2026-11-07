/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdlib.h>
#include "draw.h"
#include "gstore.h"
#include "bstore.h"
#include "tcheck.h"

#define W 23
#define H 17

static unsigned char g_a[H * W * 3];
static unsigned char g_m[H * W * 3];
static unsigned char g_t[H * W * 3];

static unsigned long g_seed = 4711;

static unsigned int rnd(unsigned int n)
{
    g_seed = g_seed * 1103515245UL + 12345UL;
    return (unsigned int) ((g_seed >> 16) % n);
}

static unsigned long px_get(const unsigned char *f, int b, int x, int y)
{
    const unsigned char *p = f + (y * W + x) * b;
    unsigned long v = p[0];

    if (b > 1) {
        v |= (unsigned long) p[1] << 8;
    }
    if (b > 2) {
        v |= (unsigned long) p[2] << 16;
    }
    return v;
}

static void px_set(unsigned char *f, int b, int x, int y, unsigned long v)
{
    unsigned char *p = f + (y * W + x) * b;

    p[0] = (unsigned char) v;
    if (b > 1) {
        p[1] = (unsigned char) (v >> 8);
    }
    if (b > 2) {
        p[2] = (unsigned char) (v >> 16);
    }
}

static unsigned long model_rop(unsigned int rop, unsigned long p,
                               unsigned long d, int bits)
{
    unsigned long r = 0;
    int i, pb, db;

    for (i = 0; i < bits; i++) {
        pb = (int) ((p >> i) & 1);
        db = (int) ((d >> i) & 1);
        if (rop & (1U << (pb << 2 | db))) {
            r |= 1UL << i;
        }
    }
    return r;
}

static unsigned long model_color(const CORE_ORDER *o, int x, int y)
{
    int row, col;

    if (o->kind != CORE_ORDER_PATTERN) {
        return o->color;
    }
    row = ((y - o->pat_y) % 8 + 8) % 8;
    col = ((x - o->pat_x) % 8 + 8) % 8;
    return (o->pat[row] & (0x80 >> col)) ? o->bg : o->color;
}

static void model(unsigned char *f, int b, const CORE_ORDER *o)
{
    int x, y;
    unsigned int rop;

    if (o->kind == CORE_ORDER_COPY) {
        memcpy(g_t, f, sizeof(g_t));
        for (y = o->rect.top; y < o->rect.bottom; y++) {
            for (x = o->rect.left; x < o->rect.right; x++) {
                px_set(f, b, x, y, px_get(g_t, b,
                       o->src_x + x - o->rect.left,
                       o->src_y + y - o->rect.top));
            }
        }
        return;
    }
    rop = o->kind == CORE_ORDER_LINE ? draw_rop3_of_rop2(o->rop) : o->rop;
    for (y = o->rect.top; y < o->rect.bottom; y++) {
        for (x = o->rect.left; x < o->rect.right; x++) {
            px_set(f, b, x, y, model_rop(rop, model_color(o, x, y),
                                         px_get(f, b, x, y), b * 8));
        }
    }
}

static void fill_random(int b)
{
    unsigned int i;

    for (i = 0; i < (unsigned int) (W * H * b); i++) {
        g_a[i] = g_m[i] = (unsigned char) rnd(256);
    }
}

static void say(const CORE_ORDER *o)
{
    printf("  order: kind %d, (%ld,%ld)-(%ld,%ld), from (%d,%d), color "
           "0x%lx, rop 0x%x\n", o->kind, (long) o->rect.left,
           (long) o->rect.top, (long) o->rect.right, (long) o->rect.bottom,
           o->src_x, o->src_y, o->color, o->rop);
}

static void run(int b, const CORE_ORDER *o, int line)
{
    static unsigned char up[H * W * 3];
    int y, rowb = W * b;

    for (y = 0; y < H; y++) {
        memcpy(up + (H - 1 - y) * rowb, g_a + y * rowb, (size_t) rowb);
    }
    draw_order(g_a, rowb, (unsigned int) b * 8, o);
    draw_order(up + (H - 1) * rowb, -rowb, (unsigned int) b * 8, o);
    model(g_m, b, o);
    if (memcmp(g_a, g_m, (size_t) (W * H * b)) != 0) {
        t_failures++;
        printf("t_draw.c(%d): %d bpp, kind %d, rop 0x%02X: pixels differ "
               "from the model\n", line, b * 8, o->kind, o->rop);
        say(o);
    }
    for (y = 0; y < H; y++) {
        if (memcmp(up + (H - 1 - y) * rowb, g_m + y * rowb,
                   (size_t) rowb) != 0) {
            t_failures++;
            printf("t_draw.c(%d): bottom-up row %d differs\n", line, y);
            say(o);
            break;
        }
    }
}

static CORE_ORDER mk(int kind, int l, int t, int r, int b,
                     unsigned long color, unsigned int rop)
{
    CORE_ORDER o;

    memset(&o, 0, sizeof(o));
    o.kind = kind;
    o.rect.left = l;
    o.rect.top = t;
    o.rect.right = r;
    o.rect.bottom = b;
    o.color = color;
    o.rop = rop;
    return o;
}

static void test_rop_helpers(void)
{
    static const unsigned int want[16] = {
        0x00, 0x05, 0x0A, 0x0F, 0x50, 0x55, 0x5A, 0x5F,
        0xA0, 0xA5, 0xAA, 0xAF, 0xF0, 0xF5, 0xFA, 0xFF
    };
    unsigned int r2;

    for (r2 = 1; r2 <= 16; r2++) {
        CHECK_EQ(draw_rop3_of_rop2(r2), want[r2 - 1]);
    }
    CHECK(!DRAW_USES_P(0x00) && !DRAW_USES_S(0x00) && !DRAW_USES_D(0x00));
    CHECK(DRAW_USES_P(0xF0) && !DRAW_USES_S(0xF0) && !DRAW_USES_D(0xF0));
    CHECK(!DRAW_USES_P(0xCC) && DRAW_USES_S(0xCC) && !DRAW_USES_D(0xCC));
    CHECK(!DRAW_USES_P(0x55) && !DRAW_USES_S(0x55) && DRAW_USES_D(0x55));
    CHECK(DRAW_USES_P(0x5A) && !DRAW_USES_S(0x5A) && DRAW_USES_D(0x5A));
    CHECK(DRAW_USES_P(0xC0) && DRAW_USES_S(0xC0) && !DRAW_USES_D(0xC0));
    CHECK_EQ(draw_rop3(0xF0, 0x1234, 0xFFFF), 0x1234);
    CHECK_EQ(draw_rop3(0x5A, 0x00FF, 0x0F0F), 0x0FF0);
    CHECK_EQ(draw_rop3(0x55, 0, 0x0F0F) & 0xFFFF, 0xF0F0);
}

static void test_fills(void)
{
    static const unsigned int rops[] = {
        0xF0, 0x00, 0xFF, 0x55, 0x5A, 0x0F, 0xA0, 0xFA, 0x50, 0xAA, 0xA5
    };
    CORE_ORDER o;
    int b, i, k;

    for (b = 1; b <= 3; b++) {
        for (i = 0; i < (int) (sizeof(rops) / sizeof(rops[0])); i++) {
            fill_random(b);
            o = mk(CORE_ORDER_FILL, 2, 3, 19, 11,
                   0x00A5C3E1UL & (b == 1 ? 0xFF : b == 2 ? 0xFFFF :
                                   0xFFFFFF), rops[i]);
            run(b, &o, __LINE__);
            fill_random(b);
            o.rect.left = 0;
            o.rect.top = 0;
            o.rect.right = W;
            o.rect.bottom = H;
            run(b, &o, __LINE__);
        }
        for (k = 1; k <= 16; k++) {
            fill_random(b);
            o = mk(CORE_ORDER_LINE, 1, 5, 22, 6, 0x00336699UL &
                   (b == 1 ? 0xFF : b == 2 ? 0xFFFF : 0xFFFFFF),
                   (unsigned int) k);
            run(b, &o, __LINE__);
            o.rect.left = 7;
            o.rect.right = 8;
            o.rect.top = 0;
            o.rect.bottom = H;
            run(b, &o, __LINE__);
        }
    }
}

static void test_copies(void)
{
    static const int moves[][2] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 3, 2 }, { -3, -2 },
        { 4, -5 }, { -4, 3 }, { 0, 0 }, { 8, -6 }, { -5, 3 }
    };
    CORE_ORDER o;
    int b, i, dx, dy;

    for (b = 1; b <= 3; b++) {
        for (i = 0; i < (int) (sizeof(moves) / sizeof(moves[0])); i++) {
            dx = moves[i][0];
            dy = moves[i][1];
            fill_random(b);
            o = mk(CORE_ORDER_COPY, 5 + dx, 6 + dy, 15 + dx, 14 + dy, 0,
                   0xCC);
            o.src_x = 5;
            o.src_y = 6;
            run(b, &o, __LINE__);
        }
        fill_random(b);
        o = mk(CORE_ORDER_COPY, 0, 0, W, H - 1, 0, 0xCC);
        o.src_x = 0;
        o.src_y = 1;
        run(b, &o, __LINE__);
    }
}

static void test_random(void)
{
    CORE_ORDER o;
    int b, n, l, t, w, h;

    for (n = 0; n < 3000; n++) {
        b = 1 + (int) rnd(3);
        fill_random(b);
        w = 1 + (int) rnd(W);
        h = 1 + (int) rnd(H);
        l = (int) rnd((unsigned int) (W - w + 1));
        t = (int) rnd((unsigned int) (H - h + 1));
        switch (rnd(3)) {
        case 0:
            o = mk(CORE_ORDER_COPY, l, t, l + w, t + h, 0, 0xCC);
            o.src_x = (int) rnd((unsigned int) (W - w + 1));
            o.src_y = (int) rnd((unsigned int) (H - h + 1));
            break;
        case 1:
            o = mk(CORE_ORDER_FILL, l, t, l + w, t + h,
                   (unsigned long) rnd(0x10000) << 8 | rnd(256), 0);
            do {
                o.rop = rnd(256);
            } while (DRAW_USES_S(o.rop));
            o.color &= b == 1 ? 0xFF : b == 2 ? 0xFFFF : 0xFFFFFF;
            break;
        default:
            if (rnd(2)) {
                h = 1;
            } else {
                w = 1;
            }
            o = mk(CORE_ORDER_LINE, l, t, l + w, t + h,
                   (unsigned long) rnd(0x10000) << 8 | rnd(256),
                   1 + rnd(16));
            o.color &= b == 1 ? 0xFF : b == 2 ? 0xFFFF : 0xFFFFFF;
            break;
        }
        run(b, &o, __LINE__);
        if (t_failures > 10) {
            break;
        }
    }
}

static CORE_GLYPH g_gl[CORE_TEXT_MAX];

static void random_glyph(CORE_GLYPH *g)
{
    unsigned int row, x, y;

    memset(g, 0, sizeof(*g));
    g->cx = (int) rnd(12) + 1;
    g->cy = (int) rnd(10) + 1;
    g->ox = (int) rnd(7) - 3;
    g->oy = -(int) rnd(11);
    row = ((unsigned int) g->cx + 7) / 8;
    for (y = 0; y < (unsigned int) g->cy; y++) {
        for (x = 0; x < (unsigned int) g->cx; x++) {
            if (rnd(2)) {
                g->bits[y * row + x / 8] |= (unsigned char) (0x80 >> (x % 8));
            }
        }
    }
}

static void model_text(unsigned char *f, int b, const CORE_ORDER *o,
                       const CORE_GLYPHREF *r, const CORE_GLYPH *const *g)
{
    int x, y, i, gx, gy, row;

    for (y = o->rect.top; y < o->rect.bottom; y++) {
        for (x = o->rect.left; x < o->rect.right; x++) {
            if (x >= o->op.left && x < o->op.right && y >= o->op.top &&
                y < o->op.bottom) {
                px_set(f, b, x, y, o->bg);
            }
            for (i = 0; i < o->nglyph; i++) {
                gx = x - (r[i].x + g[i]->ox);
                gy = y - (o->y + g[i]->oy);
                row = (g[i]->cx + 7) / 8;
                if (gx >= 0 && gx < g[i]->cx && gy >= 0 && gy < g[i]->cy &&
                    (g[i]->bits[gy * row + gx / 8] & (0x80 >> (gx % 8)))) {
                    px_set(f, b, x, y, o->color);
                }
            }
        }
    }
}

static void run_text(int b, const CORE_ORDER *o, const CORE_GLYPHREF *r,
                     const CORE_GLYPH *const *g, int line)
{
    static unsigned char up[H * W * 3];
    int y, rowb = W * b;

    for (y = 0; y < H; y++) {
        memcpy(up + (H - 1 - y) * rowb, g_a + y * rowb, (size_t) rowb);
    }
    draw_text(g_a, rowb, (unsigned int) b * 8, o, r, g);
    draw_text(up + (H - 1) * rowb, -rowb, (unsigned int) b * 8, o, r, g);
    model_text(g_m, b, o, r, g);
    if (memcmp(g_a, g_m, (size_t) (W * H * b)) != 0) {
        t_failures++;
        printf("t_draw.c(%d): %d bpp, text of %d glyphs: pixels differ "
               "from the model\n", line, b * 8, o->nglyph);
    }
    for (y = 0; y < H; y++) {
        if (memcmp(up + (H - 1 - y) * rowb, g_m + y * rowb,
                   (size_t) rowb) != 0) {
            t_failures++;
            printf("t_draw.c(%d): text: bottom-up row %d differs\n", line,
                   y);
            break;
        }
    }
}

static void test_text(void)
{
    static const unsigned char h[] = { 0x84, 0x84, 0x84, 0xFC, 0x84, 0x84,
                                       0x84 };
    const CORE_GLYPH *g[CORE_TEXT_MAX];
    CORE_GLYPHREF r[CORE_TEXT_MAX];
    CORE_ORDER o;
    int round, b, i, n, x;

    memset(&g_gl[0], 0, sizeof(g_gl[0]));
    g_gl[0].oy = -7;
    g_gl[0].cx = 6;
    g_gl[0].cy = 7;
    memcpy(g_gl[0].bits, h, sizeof(h));
    g[0] = &g_gl[0];
    memset(r, 0, sizeof(r));
    r[0].x = 2;
    memset(&o, 0, sizeof(o));
    o.kind = CORE_ORDER_TEXT;
    o.rect.right = W;
    o.rect.bottom = H;
    o.y = 9;
    o.nglyph = 1;
    o.color = 0xEE;
    memset(g_a, 0, sizeof(g_a));
    draw_text(g_a, W, 8, &o, r, g);
    CHECK_EQ(px_get(g_a, 1, 2, 2), 0xEE);
    CHECK_EQ(px_get(g_a, 1, 7, 8), 0xEE);
    CHECK_EQ(px_get(g_a, 1, 4, 5), 0xEE);
    CHECK_EQ(px_get(g_a, 1, 4, 4), 0);
    CHECK_EQ(px_get(g_a, 1, 2, 9), 0);
    CHECK_EQ(px_get(g_a, 1, 8, 2), 0);

    for (round = 0; round < 3000; round++) {
        b = (int) rnd(3) + 1;
        n = (int) rnd(CORE_TEXT_MAX) + 1;
        if (round % 3 != 0) {
            n = (int) rnd(6) + 1;
        }
        x = (int) rnd(W + 6) - 6;
        for (i = 0; i < n; i++) {
            random_glyph(&g_gl[i]);
            g[i] = &g_gl[i];
            r[i].x = x;
            x += (int) rnd(9);
        }
        memset(&o, 0, sizeof(o));
        o.kind = CORE_ORDER_TEXT;
        o.rect.left = (int) rnd(W);
        o.rect.top = (int) rnd(H);
        o.rect.right = o.rect.left + 1 + (int) rnd((unsigned int) (W -
                                                   o.rect.left));
        o.rect.bottom = o.rect.top + 1 + (int) rnd((unsigned int) (H -
                                                   o.rect.top));
        if (rnd(2)) {
            o.op.left = (int) rnd(W + 8) - 4;
            o.op.top = (int) rnd(H + 8) - 4;
            o.op.right = o.op.left + (int) rnd(W);
            o.op.bottom = o.op.top + (int) rnd(H);
        }
        o.y = (int) rnd(H + 10);
        o.nglyph = n;
        o.color = rnd(0x10000) | ((unsigned long) rnd(256) << 16);
        o.bg = rnd(0x10000) | ((unsigned long) rnd(256) << 16);
        if (b < 3) {
            o.color &= b == 1 ? 0xFFUL : 0xFFFFUL;
            o.bg &= b == 1 ? 0xFFUL : 0xFFFFUL;
        }
        fill_random(b);
        run_text(b, &o, r, g, __LINE__);
    }
}

static void test_gstore(void)
{
    GSTORE *s = gstore_new();
    CORE_GLYPH a, c;
    CORE_GLYPHREF r[3], out[3];
    unsigned long sa, sb, sc, first, run;
    unsigned int ia, ib, ic, i;

    CHECK(s != NULL);
    if (s == NULL) {
        return;
    }
    random_glyph(&a);
    c = a;
    c.bits[0] ^= 0x80;
    ia = gstore_put(s, &a, &sa);
    ib = gstore_put(s, &a, &sb);
    CHECK_EQ(ia, ib);
    CHECK_EQ(sa, sb);
    CHECK(sa != 0);
    ic = gstore_put(s, &c, &sc);
    CHECK(ic != ia);
    CHECK(gstore_get(s, ia, sa) != NULL);
    CHECK(gstore_get(s, ia, sa + 1) == NULL);
    CHECK(gstore_get(s, GSTORE_GLYPHS, 1) == NULL);
    CHECK_EQ(gstore_get(s, ic, sc)->bits[0], c.bits[0]);
    c.ox++;
    CHECK(gstore_put(s, &c, &sb) != ic);
    for (i = 0; i < GSTORE_GLYPHS; i++) {
        c.cx = 1 + (int) (i % 32);
        c.cy = 1 + (int) (i / 32);
        gstore_put(s, &a, &sb);
        gstore_put(s, &c, &sb);
    }
    CHECK(gstore_get(s, ia, sa) != NULL);
    CHECK(gstore_get(s, ic, sc) == NULL);
    for (i = 0; i < 3; i++) {
        r[i].id = i;
        r[i].serial = 100 + i;
        r[i].x = 10 * (int) i;
    }
    first = gstore_log(s, r, 3);
    CHECK_EQ(gstore_run(s, first, 3, out), 1);
    CHECK_EQ(out[2].x, 20);
    CHECK_EQ(out[1].serial, 101);
    CHECK_EQ(gstore_run(s, first + 1, 3, out), 0);
    run = first;
    for (i = 0; i < CORE_GLYPH_LOG / 3; i++) {
        gstore_log(s, r, 3);
    }
    CHECK_EQ(gstore_run(s, run, 3, out), 0);
    CHECK_EQ(gstore_run(s, run + 3, 3, out), 1);
    gstore_free(s);
}

static unsigned long model_sd(unsigned int rop, unsigned long s,
                              unsigned long d, int bits)
{
    unsigned long r = 0;
    int i, sb, db;

    for (i = 0; i < bits; i++) {
        sb = (int) ((s >> i) & 1);
        db = (int) ((d >> i) & 1);
        if ((rop >> (sb << 1 | db)) & 1) {
            r |= 1UL << i;
        }
    }
    return r;
}

static void test_bitmap_draw(void)
{
    static unsigned char bits[13 * 11 * 3];
    CORE_BITMAP bm;
    CORE_ORDER o;
    unsigned long s, d;
    int b, k, x, y, rop, w, h;
    long i;

    CHECK_EQ(draw_rop3_sd(0xCC, 0x1234, 0x0F0F), 0x1234);
    CHECK_EQ(draw_rop3_sd(0x88, 0x00FF, 0x0F0F), 0x000F);
    CHECK_EQ(draw_rop3_sd(0x66, 0x00FF, 0x0F0F), 0x0FF0);
    CHECK_EQ(draw_rop3_sd(0xEE, 0x00FF, 0x0F0F), 0x0FFF);
    CHECK_EQ(draw_rop3_sd(0x33, 0x00FF, 0) & 0xFFFF, 0xFF00);
    CHECK_EQ(draw_rop3_sd(0x44, 0x00FF, 0x0F0F), 0x00F0);
    for (k = 0; k < 600; k++) {
        b = 1 + (int) rnd(3);
        do {
            rop = (int) rnd(16);
            rop |= rop << 4;
        } while (!DRAW_USES_S((unsigned int) rop));
        for (i = 0; i < (long) sizeof(bits); i++) {
            bits[i] = (unsigned char) rnd(256);
        }
        for (i = 0; i < (long) sizeof(g_a); i++) {
            g_a[i] = (unsigned char) rnd(256);
        }
        memcpy(g_m, g_a, sizeof(g_a));
        bm.cx = 13;
        bm.cy = 11;
        bm.bits = bits;
        bm.stride = 13L * b;
        x = (int) rnd(W);
        y = (int) rnd(H);
        w = 1 + (int) rnd((unsigned int) (W - x));
        h = 1 + (int) rnd((unsigned int) (H - y));
        w = w > 13 ? 13 : w;
        h = h > 11 ? 11 : h;
        o = mk(CORE_ORDER_BITMAP, x, y, x + w, y + h, 0, (unsigned int) rop);
        o.src_x = (int) rnd((unsigned int) (13 - w + 1));
        o.src_y = (int) rnd((unsigned int) (11 - h + 1));
        draw_bitmap(g_a, W * b, (unsigned int) b * 8, &o, &bm);
        for (y = (int) o.rect.top; y < (int) o.rect.bottom; y++) {
            for (x = (int) o.rect.left; x < (int) o.rect.right; x++) {
                s = 0;
                for (i = b - 1; i >= 0; i--) {
                    s = s << 8 |
                        bits[(o.src_y + y - o.rect.top) * 13L * b +
                             (o.src_x + x - o.rect.left) * (long) b + i];
                }
                d = px_get(g_m, b, x, y);
                px_set(g_m, b, x, y,
                       model_sd((unsigned int) rop, s, d, 8 * b));
            }
        }
        CHECK_MEM(g_a, g_m, sizeof(g_a));
    }
}

static void test_bstore(void)
{
    static unsigned char pix[64 * 64 * 3];
    static const unsigned char two[2][8] = {
        { 1, 2, 3, 4, 5, 6, 0xEE, 0xEE },
        { 7, 8, 9, 10, 11, 12, 0xEE, 0xEE }
    };
    const CORE_BITMAP *g;
    BSTORE *s = bstore_new();
    unsigned long key[2], sa, sb, sc;
    int ia, ib, ic, i;

    bstore_key(3, 2, 16, &two[0][0], 8, key);
    CHECK_EQ(key[0], 0x3EBC2B20UL);
    CHECK_EQ(key[1], 0xF3B87CC0UL);
    CHECK(s != NULL);
    if (s == NULL) {
        return;
    }
    CHECK_EQ(bstore_put(s, 0, 2, 16, &two[0][0], 8, &sa), -1);
    CHECK_EQ(bstore_put(s, 3, 2, 12, &two[0][0], 8, &sa), -1);
    ia = bstore_put(s, 3, 2, 16, &two[0][0], 8, &sa);
    CHECK(ia >= 0 && sa != 0);
    g = bstore_get(s, (unsigned int) ia, sa);
    CHECK(g != NULL);
    if (g != NULL) {
        CHECK_EQ(g->cx, 3);
        CHECK_EQ(g->stride, 6);
        CHECK_EQ(g->key[0], 0x3EBC2B20UL);
        CHECK_EQ(g->bits[6], 7);
    }
    CHECK(bstore_get(s, (unsigned int) ia, sa + 1) == NULL);
    CHECK(bstore_get(s, BSTORE_PLACES, sa) == NULL);
    memcpy(pix, two, sizeof(two));
    ib = bstore_put(s, 3, 2, 16, pix, 8, &sb);
    CHECK_EQ(ib, ia);
    CHECK_EQ(sb, sa);
    pix[9] ^= 1;
    ic = bstore_put(s, 3, 2, 16, pix, 8, &sc);
    CHECK(ic >= 0 && ic != ia);
    for (i = 0; i < BSTORE_PLACES; i++) {
        pix[0] = (unsigned char) i;
        pix[1] = (unsigned char) (i >> 8);
        CHECK(bstore_put(s, 3, 2, 16, &two[0][0], 8, &sb) == ia);
        CHECK(bstore_put(s, 64, 64, 24, pix, 64 * 3, &sb) >= 0);
    }
    CHECK(bstore_get(s, (unsigned int) ia, sa) != NULL);
    CHECK(bstore_get(s, (unsigned int) ic, sc) == NULL);
    bstore_clear(s);
    CHECK(bstore_get(s, (unsigned int) ia, sa) == NULL);
    ib = bstore_put(s, 3, 2, 16, &two[0][0], 8, &sb);
    CHECK(ib >= 0 && sb != 0);
    bstore_free(s);
}

static CORE_ORDER mk_pat(int l, int t, int r, int b, unsigned long c0,
                         unsigned long c1, unsigned int rop, int ox, int oy,
                         const unsigned char *pat)
{
    CORE_ORDER o = mk(CORE_ORDER_PATTERN, l, t, r, b, c0, rop);

    o.bg = c1;
    o.pat_x = ox;
    o.pat_y = oy;
    memcpy(o.pat, pat, 8);
    return o;
}

static const unsigned char g_gray[8] = {
    0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55
};
static const unsigned char g_odd[8] = {
    0x80, 0x41, 0x22, 0x14, 0x0F, 0xF0, 0x3C, 0x01
};

static void test_patterns(void)
{
    unsigned char pat[8];
    CORE_ORDER o;
    int b, i, k, l, t;

    o = mk_pat(0, 0, 1, 1, 0, 1, 0xF0, 3, 5, g_odd);
    CHECK_EQ(draw_pat_bit(&o, 3, 5), 1);
    CHECK_EQ(draw_pat_bit(&o, 4, 5), 0);
    CHECK_EQ(draw_pat_bit(&o, 2, 5), 0);
    CHECK_EQ(draw_pat_bit(&o, 10, 12), 1);
    CHECK_EQ(draw_pat_bit(&o, -5, -3), 1);
    CHECK_EQ(draw_pat_bit(&o, 7, 9), 1);
    CHECK_EQ(draw_pat_bit(&o, 6, 9), 0);
    for (b = 1; b <= 3; b++) {
        fill_random(b);
        o = mk_pat(1, 2, 20, 15, 0x123456UL, 0x0F0F0FUL, 0xF0, 3, 6,
                   g_odd);
        run(b, &o, __LINE__);
        o = mk_pat(0, 0, W, H, 0x000000UL, 0xFFFFFFUL, 0x5A, 0, 0, g_gray);
        run(b, &o, __LINE__);
        o = mk_pat(5, 0, 6, H, 0x55AA55UL, 0x0000FFUL, 0x5A, 7, 1, g_odd);
        run(b, &o, __LINE__);
    }
    for (i = 0; i < 400; i++) {
        b = 1 + (int) rnd(3);
        fill_random(b);
        for (k = 0; k < 8; k++) {
            pat[k] = (unsigned char) rnd(256);
        }
        l = (int) rnd(W);
        t = (int) rnd(H);
        o = mk_pat(l, t, l + 1 + (int) rnd((unsigned int) (W - l)),
                   t + 1 + (int) rnd((unsigned int) (H - t)),
                   (unsigned long) rnd(65536) << 8 | rnd(256),
                   (unsigned long) rnd(65536) << 8 | rnd(256),
                   draw_rop3_of_rop2(1 + (unsigned int) (i % 16)),
                   (int) rnd(8), (int) rnd(8), pat);
        o.color &= b == 1 ? 0xFFUL : b == 2 ? 0xFFFFUL : 0xFFFFFFUL;
        o.bg &= b == 1 ? 0xFFUL : b == 2 ? 0xFFFFUL : 0xFFFFFFUL;
        run(b, &o, __LINE__);
    }
}

static int split_check(int b, const CORE_ORDER *o, int bands, int max,
                       CORE_ORDER *part, int line)
{
    static unsigned char f1[H * W * 3], f2[H * W * 3];
    static unsigned char cover[H * W];
    int n, i, x, y, ok = 1, rowb = W * b;

    n = draw_pat_split(g_a, rowb, (unsigned int) b * 8, o, bands, part,
                       max);
    if (n <= 0) {
        return n;
    }
    memset(cover, 0, sizeof(cover));
    memcpy(f1, g_a, sizeof(f1));
    memcpy(f2, g_a, sizeof(f2));
    for (i = 0; i < n; i++) {
        if (part[i].rect.left < o->rect.left ||
            part[i].rect.top < o->rect.top ||
            part[i].rect.right > o->rect.right ||
            part[i].rect.bottom > o->rect.bottom ||
            part[i].rect.left >= part[i].rect.right ||
            part[i].rect.top >= part[i].rect.bottom) {
            ok = 0;
        }
        for (y = part[i].rect.top; ok && y < part[i].rect.bottom; y++) {
            for (x = part[i].rect.left; x < part[i].rect.right; x++) {
                cover[y * W + x]++;
            }
        }
        if (DRAW_USES_D(part[i].rop) &&
            (part[i].kind != o->kind || part[i].rop != o->rop ||
             part[i].color != o->color || part[i].bg != o->bg ||
             memcmp(part[i].pat, o->pat, 8) != 0 ||
             part[i].pat_x != o->pat_x || part[i].pat_y != o->pat_y)) {
            ok = 0;
        }
        draw_order(f1, rowb, (unsigned int) b * 8, &part[i]);
    }
    for (y = 0; ok && y < H; y++) {
        for (x = 0; x < W; x++) {
            if (cover[y * W + x] != (x >= o->rect.left &&
                                     x < o->rect.right &&
                                     y >= o->rect.top &&
                                     y < o->rect.bottom)) {
                ok = 0;
            }
        }
    }
    draw_order(f2, rowb, (unsigned int) b * 8, o);
    if (!ok || memcmp(f1, f2, (size_t) (W * H * b)) != 0) {
        t_failures++;
        printf("t_draw.c(%d): draw_pat_split, %d bpp: %d parts do not "
               "match the order\n", line, b * 8, n);
        say(o);
    }
    return n;
}

static void test_pat_split(void)
{
    CORE_ORDER o, part[16], t;
    int b, n, i, k, x, y, l;

    for (b = 1; b <= 3; b++) {
        memset(g_a, 0, sizeof(g_a));
        for (y = 0; y < H; y++) {
            for (x = 0; x < W; x++) {
                px_set(g_a, b, x, y, 6);
            }
        }
        o = mk_pat(2, 3, 20, 7, 0, 15, 0x5A, 0, 0, g_gray);
        n = split_check(b, &o, 0, 16, part, __LINE__);
        CHECK_EQ(n, 1);
        CHECK_EQ(part[0].kind, CORE_ORDER_PATTERN);
        CHECK_EQ(part[0].rop, 0xF0U);
        CHECK_EQ(part[0].color, 6UL);
        CHECK_EQ(part[0].bg, 9UL);
        t = mk_pat(4, 0, 5, H, 0, 15, 0x5A, 0, 0, g_gray);
        n = split_check(b, &t, 0, 16, part, __LINE__);
        CHECK_EQ(n, 1);
        CHECK_EQ(part[0].kind, CORE_ORDER_PATTERN);
        draw_order(g_a, W * b, (unsigned int) b * 8, &o);
        n = split_check(b, &o, 0, 16, part, __LINE__);
        CHECK_EQ(n, 1);
        CHECK_EQ(part[0].kind, CORE_ORDER_FILL);
        CHECK_EQ(part[0].rop, 0xF0U);
        CHECK_EQ(part[0].color, 6UL);
        CHECK_EQ(split_check(b, &t, 0, 16, part, __LINE__), 0);
        CHECK_EQ(split_check(b, &t, 1, 16, part, __LINE__), 3);
    }

    b = 1;
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            px_set(g_a, b, x, y, x < 9 ? 6 : 15);
        }
    }
    o = mk_pat(1, 4, 22, 8, 0, 15, 0x5A, 0, 0, g_gray);
    CHECK_EQ(split_check(b, &o, 0, 16, part, __LINE__), 0);
    n = split_check(b, &o, 1, 16, part, __LINE__);
    CHECK_EQ(n, 2);
    CHECK_EQ(part[0].rect.right, 9L);
    CHECK_EQ(part[1].color, 15UL);
    CHECK_EQ(part[1].bg, 0UL);
    CHECK_EQ(split_check(b, &o, 1, 1, part, __LINE__), 0);

    for (y = 0; y < H; y++) {
        for (x = 10; x < 13; x++) {
            px_set(g_a, b, x, y, (y * 7 + x * 3) % 16);
        }
    }
    n = split_check(b, &o, 1, 16, part, __LINE__);
    CHECK_EQ(n, 4);
    CHECK_EQ(part[2].rect.left, 10L);
    CHECK_EQ(part[2].rect.right, 13L);
    CHECK_EQ(part[2].rop, 0x5AU);

    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            px_set(g_a, b, x, y, y < 5 ? 1 : y < 11 ? 7 : 8);
        }
    }
    o = mk_pat(3, 0, 7, H, 0, 15, 0x5A, 1, 2, g_odd);
    n = split_check(b, &o, 1, 16, part, __LINE__);
    CHECK_EQ(n, 3);
    CHECK_EQ(part[1].rect.top, 5L);
    CHECK_EQ(part[1].rect.bottom, 11L);

    o.rop = 0xF0;
    CHECK_EQ(split_check(b, &o, 1, 16, part, __LINE__), 0);

    for (i = 0; i < 600; i++) {
        b = 1 + (int) rnd(3);
        l = 1 + (int) rnd(4);
        for (y = 0; y < H; y++) {
            for (x = 0; x < W; x++) {
                px_set(g_a, b, x, y, (unsigned long) ((x / l + y / 3 +
                       (int) rnd(i % 3 == 0 ? 3 : 1)) % 3) * 0x050301UL);
            }
        }
        for (k = 0; k < 8; k++) {
            t.pat[k] = (unsigned char) rnd(256);
        }
        x = (int) rnd(W);
        y = (int) rnd(H);
        do {
            k = 1 + (int) rnd(16);
        } while (!DRAW_USES_D(draw_rop3_of_rop2((unsigned int) k)));
        o = mk_pat(x, y, x + 1 + (int) rnd((unsigned int) (W - x)),
                   y + 1 + (int) rnd((unsigned int) (H - y)),
                   (unsigned long) rnd(256) * 0x010101UL,
                   (unsigned long) rnd(256) * 0x010101UL,
                   draw_rop3_of_rop2((unsigned int) k), (int) rnd(8),
                   (int) rnd(8), t.pat);
        o.color &= b == 1 ? 0xFFUL : b == 2 ? 0xFFFFUL : 0xFFFFFFUL;
        o.bg &= b == 1 ? 0xFFUL : b == 2 ? 0xFFFFUL : 0xFFFFFFUL;
        split_check(b, &o, (int) (i & 1), 1 + (int) rnd(16), part,
                    __LINE__);
    }
}

static void test_pat_rgb8(void)
{
    static unsigned char pal[256 * 3];
    CORE_ORDER o;
    int x, y;

    memset(pal, 0, sizeof(pal));
    pal[6 * 3 + 1] = pal[6 * 3 + 2] = 0x80;
    pal[9 * 3] = 0xFF;
    pal[7 * 3] = pal[7 * 3 + 1] = pal[7 * 3 + 2] = 0xC0;
    pal[8 * 3] = pal[8 * 3 + 1] = pal[8 * 3 + 2] = 0x80;
    pal[15 * 3] = pal[15 * 3 + 1] = pal[15 * 3 + 2] = 0xFF;
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            g_a[y * W + x] = (unsigned char) ((x + y) % 3 ? 15 : 0);
        }
    }
    o = mk_pat(0, 0, W, H, 0, 15, 0x5A, 0, 0, g_gray);
    CHECK_EQ(draw_pat_rgb8(g_a, W, &o, pal), 1);
    g_a[5 * W + 5] = 6;
    CHECK_EQ(draw_pat_rgb8(g_a, W, &o, pal), 0);
    o = mk_pat(5, 5, 6, 6, 0, 15, 0x5A, 5, 5, g_odd);
    CHECK_EQ(draw_pat_rgb8(g_a, W, &o, pal), 0);
    o.pat_x = 4;
    CHECK_EQ(draw_pat_rgb8(g_a, W, &o, pal), 1);
    g_a[5 * W + 5] = 7;
    o.pat_x = 5;
    CHECK_EQ(draw_pat_rgb8(g_a, W, &o, pal), 0);
    o.rop = 0xF0;
    CHECK_EQ(draw_pat_rgb8(g_a, W, &o, pal), 1);
}

int main(void)
{
    test_rop_helpers();
    test_fills();
    test_copies();
    test_random();
    test_text();
    test_gstore();
    test_bitmap_draw();
    test_bstore();
    test_patterns();
    test_pat_split();
    test_pat_rgb8();
    return t_result("t_draw");
}
