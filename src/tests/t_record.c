/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include "record.h"
#include "tcheck.h"

static REC_LIST g_l;
static MR_OUT g_o;

static void take(void)
{
    memset(&g_o, 0, sizeof(g_o));
    rec_take(&g_l, &g_o);
}

static int inside(const MR_RECT *p, LONG l, LONG t, LONG r, LONG b)
{
    return l >= p->left && t >= p->top && r <= p->right && b <= p->bottom;
}

static int covered(LONG l, LONG t, LONG r, LONG b)
{
    ULONG i;

    for (i = 0; i < g_o.nrects; i++) {
        if (inside(&g_o.rect[i], l, t, r, b)) {
            return 1;
        }
    }
    return 0;
}

static void test_basic(void)
{
    rec_init(&g_l, 640, 480);
    CHECK_EQ(rec_add(&g_l, 10, 20, 30, 40), 1);
    CHECK_EQ(g_l.n, 1);
    CHECK_EQ(rec_add(&g_l, 100, 100, 110, 110), 0);
    CHECK_EQ(g_l.n, 2);
    CHECK_EQ(rec_add(&g_l, 12, 22, 28, 38), 0);
    CHECK_EQ(g_l.n, 2);
    CHECK_EQ(g_l.calls, 3);

    take();
    CHECK_EQ(g_o.flags, 0);
    CHECK_EQ(g_o.nrects, 2);
    CHECK_EQ(g_o.calls, 3);
    CHECK(inside(&g_o.rect[0], 10, 20, 30, 40));
    CHECK_EQ(g_o.rect[0].left, 10);
    CHECK_EQ(g_o.rect[0].bottom, 40);
    CHECK_EQ(g_o.rect[1].right, 110);
    CHECK_EQ(g_l.n, 0);

    CHECK_EQ(rec_add(&g_l, 0, 0, 1, 1), 1);
}

static void test_clip_and_empty(void)
{
    rec_init(&g_l, 640, 480);
    CHECK_EQ(rec_add(&g_l, 5, 5, 5, 10), 0);
    CHECK_EQ(rec_add(&g_l, 10, 10, 5, 5), 0);
    CHECK_EQ(rec_add(&g_l, 700, 0, 800, 10), 0);
    CHECK_EQ(rec_add(&g_l, -50, -50, -1, -1), 0);
    CHECK_EQ(g_l.n, 0);
    CHECK_EQ(g_l.calls, 0);

    CHECK_EQ(rec_add(&g_l, -10, -20, 5, 6), 1);
    CHECK_EQ(rec_add(&g_l, 600, 470, 9999, 9999), 0);
    take();
    CHECK_EQ(g_o.nrects, 2);
    CHECK_EQ(g_o.rect[0].left, 0);
    CHECK_EQ(g_o.rect[0].top, 0);
    CHECK_EQ(g_o.rect[0].right, 5);
    CHECK_EQ(g_o.rect[0].bottom, 6);
    CHECK_EQ(g_o.rect[1].right, 640);
    CHECK_EQ(g_o.rect[1].bottom, 480);
}

static void test_join(void)
{
    rec_init(&g_l, 640, 480);
    rec_add(&g_l, 0, 100, 40, 116);
    rec_add(&g_l, 40, 100, 90, 116);
    rec_add(&g_l, 90, 100, 95, 116);
    CHECK_EQ(g_l.n, 1);
    rec_add(&g_l, 300, 300, 310, 310);
    CHECK_EQ(g_l.n, 2);
    take();
    CHECK_EQ(g_o.rect[0].left, 0);
    CHECK_EQ(g_o.rect[0].right, 95);
    CHECK_EQ(g_o.rect[0].top, 100);
    CHECK_EQ(g_o.rect[0].bottom, 116);
}

static void test_full(void)
{
    LONG i, x, y;

    rec_init(&g_l, 1024, 768);
    for (i = 0; i < MR_RECTS; i++) {
        x = (i % 8) * 100;
        y = (i / 8) * 90;
        rec_add(&g_l, x, y, x + 4, y + 4);
    }
    CHECK_EQ(g_l.n, MR_RECTS);
    CHECK_EQ(rec_add(&g_l, 106, 90, 110, 94), 0);
    CHECK_EQ(g_l.n, MR_RECTS);
    take();
    CHECK_EQ(g_o.nrects, MR_RECTS);
    CHECK_EQ(g_o.rect[9].left, 100);
    CHECK_EQ(g_o.rect[9].right, 110);
    CHECK_EQ(g_o.rect[9].bottom, 94);
    for (i = 0; i < MR_RECTS; i++) {
        x = (i % 8) * 100;
        y = (i / 8) * 90;
        CHECK(covered(x, y, x + 4, y + 4));
    }
    CHECK(covered(106, 90, 110, 94));
}

static void test_flags(void)
{
    rec_init(&g_l, 800, 600);
    CHECK_EQ(rec_flag(&g_l, 0), 0);
    CHECK_EQ(rec_flag(&g_l, 0x100), 0);
    CHECK_EQ(rec_flag(&g_l, MR_F_PALETTE), 1);
    CHECK_EQ(rec_flag(&g_l, MR_F_MODE), 0);
    CHECK_EQ(rec_add(&g_l, 1, 1, 2, 2), 0);
    take();
    CHECK_EQ(g_o.flags, MR_F_PALETTE | MR_F_MODE);
    CHECK_EQ(g_o.nrects, 1);

    rec_add(&g_l, 1, 1, 2, 2);
    CHECK_EQ(rec_flag(&g_l, MR_F_ALL), 0);
    CHECK_EQ(g_l.n, 0);
    CHECK_EQ(rec_add(&g_l, 5, 5, 50, 50), 0);
    CHECK_EQ(g_l.n, 0);
    take();
    CHECK_EQ(g_o.flags, MR_F_ALL);
    CHECK_EQ(g_o.nrects, 0);
    CHECK_EQ(rec_flag(&g_l, MR_F_ALL), 1);
}

static unsigned long g_seed = 12345;

static LONG rnd(LONG n)
{
    g_seed = g_seed * 1103515245UL + 12345UL;
    return (LONG) ((g_seed >> 16) % (unsigned long) n);
}

static void test_random(void)
{
    static LONG in[500][4];
    LONG round, i, n, l, t, w, h;

    for (round = 0; round < 40; round++) {
        rec_init(&g_l, 1280, 1024);
        n = 1 + rnd(500);
        for (i = 0; i < n; i++) {
            l = rnd(1400) - 60;
            t = rnd(1100) - 40;
            w = 1 + rnd(round < 20 ? 40 : 400);
            h = 1 + rnd(round < 20 ? 30 : 300);
            rec_add(&g_l, l, t, l + w, t + h);
            in[i][0] = l < 0 ? 0 : l;
            in[i][1] = t < 0 ? 0 : t;
            in[i][2] = l + w > 1280 ? 1280 : l + w;
            in[i][3] = t + h > 1024 ? 1024 : t + h;
        }
        take();
        CHECK(g_o.nrects <= MR_RECTS);
        for (i = 0; i < n; i++) {
            if (in[i][0] >= in[i][2] || in[i][1] >= in[i][3]) {
                continue;
            }
            if (!covered(in[i][0], in[i][1], in[i][2], in[i][3])) {
                CHECK(0);
                printf("  round %ld: rectangle %ld lost\n", round, i);
                break;
            }
        }
        for (i = 0; i < (LONG) g_o.nrects; i++) {
            CHECK(g_o.rect[i].left >= 0 && g_o.rect[i].top >= 0);
            CHECK(g_o.rect[i].right <= 1280 && g_o.rect[i].bottom <= 1024);
            CHECK(g_o.rect[i].left < g_o.rect[i].right);
            CHECK(g_o.rect[i].top < g_o.rect[i].bottom);
        }
    }
}

static MR_RECT g_f[MR_RECTS];

static int same(const MR_RECT *p, LONG l, LONG t, LONG r, LONG b)
{
    return p->left == l && p->top == t && p->right == r && p->bottom == b;
}

static void test_flush_basic(void)
{
    ULONG fl, k;

    rec_init(&g_l, 640, 480);
    rec_add(&g_l, 10, 20, 30, 40);
    rec_add(&g_l, 100, 100, 110, 110);
    k = rec_flush(&g_l, 16, 100000, g_f, &fl);
    CHECK_EQ(k, 2);
    CHECK_EQ(fl, 0);
    CHECK(same(&g_f[0], 10, 20, 30, 40));
    CHECK(same(&g_f[1], 100, 100, 110, 110));
    CHECK_EQ(g_l.n, 0);
    CHECK_EQ(rec_add(&g_l, 1, 1, 2, 2), 1);

    rec_init(&g_l, 640, 480);
    rec_add(&g_l, 10, 20, 30, 40);
    rec_add(&g_l, 100, 100, 110, 110);
    k = rec_flush(&g_l, 16, 800 + 60 + 19, g_f, &fl);
    CHECK_EQ(k, 2);
    CHECK_EQ(fl, MR_F_MORE);
    CHECK(same(&g_f[1], 100, 100, 110, 103));
    CHECK_EQ(g_l.n, 1);
    CHECK(same(&g_l.r[0], 100, 103, 110, 110));
    CHECK_EQ(rec_add(&g_l, 300, 300, 310, 310), 0);
    k = rec_flush(&g_l, 16, 100000, g_f, &fl);
    CHECK_EQ(k, 2);
    CHECK(same(&g_f[0], 100, 103, 110, 110));
    CHECK(same(&g_f[1], 300, 300, 310, 310));
    CHECK_EQ(fl, 0);

    rec_init(&g_l, 640, 480);
    rec_add(&g_l, 0, 0, 100, 2);
    k = rec_flush(&g_l, 32, 399, g_f, &fl);
    CHECK_EQ(k, 0);
    CHECK_EQ(fl, MR_F_MORE);
    CHECK(same(&g_l.r[0], 0, 0, 100, 2));
}

static void test_flush_all(void)
{
    ULONG fl, k, rows = 0;

    rec_init(&g_l, 1280, 1024);
    rec_add(&g_l, 5, 5, 50, 50);
    rec_flag(&g_l, MR_F_ALL | MR_F_MODE);
    k = rec_flush(&g_l, 16, 1280 * 2 * 100, g_f, &fl);
    CHECK_EQ(k, 1);
    CHECK_EQ(fl, MR_F_ALL | MR_F_MODE | MR_F_MORE);
    CHECK(same(&g_f[0], 0, 0, 1280, 100));
    rows += 100;
    while (fl & MR_F_MORE) {
        k = rec_flush(&g_l, 16, 1280 * 2 * 300, g_f, &fl);
        CHECK_EQ(k, 1);
        CHECK_EQ(fl & (MR_F_ALL | MR_F_MODE), 0);
        CHECK_EQ(g_f[0].top, rows);
        rows += (ULONG) (g_f[0].bottom - g_f[0].top);
    }
    CHECK_EQ(rows, 1024);

    rec_init(&g_l, 800, 600);
    rec_add(&g_l, 0, 0, 10, 10);
    k = rec_flush(&g_l, 24, 10 * 3 * 4, g_f, &fl);
    CHECK_EQ(k, 1);
    CHECK(same(&g_f[0], 0, 0, 10, 4));
}

static void test_flush_4bpp(void)
{
    ULONG fl, k;

    rec_init(&g_l, 640, 480);
    rec_add(&g_l, 3, 0, 8, 1);
    rec_add(&g_l, 100, 10, 105, 11);
    k = rec_flush(&g_l, 4, 1000, g_f, &fl);
    CHECK_EQ(k, 2);
    CHECK(same(&g_f[0], 2, 0, 8, 1));
    CHECK(same(&g_f[1], 100, 10, 106, 11));
    CHECK_EQ(MR_ROW_BYTES(2, 8, 4), 3);
}

static unsigned char g_pend[150][200];

static void test_flush_random(void)
{
    static const ULONG bpps[5] = { 4, 8, 16, 24, 32 };
    LONG round, step, l, t, w, h, x, y, i;
    ULONG fl, k, room, bpp, used;
    int left;

    for (round = 0; round < 60; round++) {
        bpp = bpps[round % 5];
        rec_init(&g_l, 200, 150);
        memset(g_pend, 0, sizeof(g_pend));
        for (step = 0; step < 300; step++) {
            if (rnd(3) != 0) {
                l = rnd(240) - 20;
                t = rnd(180) - 15;
                w = 1 + rnd(round < 30 ? 20 : 120);
                h = 1 + rnd(round < 30 ? 15 : 90);
                if (rnd(50) == 0) {
                    rec_flag(&g_l, MR_F_ALL);
                    memset(g_pend, 1, sizeof(g_pend));
                }
                rec_add(&g_l, l, t, l + w, t + h);
                for (y = t < 0 ? 0 : t; y < t + h && y < 150; y++) {
                    for (x = l < 0 ? 0 : l; x < l + w && x < 200; x++) {
                        g_pend[y][x] = 1;
                    }
                }
                continue;
            }
            room = (ULONG) rnd(round < 30 ? 2000 : 20000);
            k = rec_flush(&g_l, bpp, room, g_f, &fl);
            CHECK(k <= MR_RECTS);
            used = 0;
            for (i = 0; i < (LONG) k; i++) {
                CHECK(g_f[i].left >= 0 && g_f[i].top >= 0);
                CHECK(g_f[i].right <= 200 && g_f[i].bottom <= 150);
                CHECK(g_f[i].left < g_f[i].right);
                CHECK(g_f[i].top < g_f[i].bottom);
                if (bpp == 4) {
                    CHECK((g_f[i].left & 1) == 0 && (g_f[i].right & 1) == 0);
                }
                used += MR_ROW_BYTES(g_f[i].left, g_f[i].right, bpp) *
                        (ULONG) (g_f[i].bottom - g_f[i].top);
                for (y = g_f[i].top; y < g_f[i].bottom; y++) {
                    for (x = g_f[i].left; x < g_f[i].right; x++) {
                        g_pend[y][x] = 0;
                    }
                }
            }
            CHECK(used <= room);
            CHECK(((fl & MR_F_MORE) != 0) == (g_l.n != 0));
        }
        do {
            k = rec_flush(&g_l, bpp, 5000, g_f, &fl);
            for (i = 0; i < (LONG) k; i++) {
                for (y = g_f[i].top; y < g_f[i].bottom; y++) {
                    for (x = g_f[i].left; x < g_f[i].right; x++) {
                        g_pend[y][x] = 0;
                    }
                }
            }
        } while (fl & MR_F_MORE);
        left = 0;
        for (y = 0; y < 150; y++) {
            for (x = 0; x < 200; x++) {
                left += g_pend[y][x];
            }
        }
        if (left != 0) {
            CHECK(0);
            printf("  flush round %ld (%lu bpp): %d pixels lost\n", round,
                   bpp, left);
        }
    }
}

static void test_move(void)
{
    rec_init(&g_l, 640, 480);
    CHECK_EQ(rec_move(&g_l, 0, 0, 640, 480, 0, -12), 0);
    CHECK_EQ(g_l.n, 0);
    rec_add(&g_l, 100, 400, 300, 412);
    CHECK_EQ(rec_move(&g_l, 0, 0, 640, 412, 0, -12), 0);
    CHECK_EQ(g_l.n, 1);
    CHECK(inside(&g_l.r[0], 100, 388, 300, 412));
    CHECK_EQ(g_l.r[0].top, 388);
    rec_init(&g_l, 640, 480);
    rec_add(&g_l, 10, 10, 50, 50);
    rec_move(&g_l, 30, 0, 640, 480, 100, 0);
    CHECK_EQ(g_l.n, 2);
    CHECK_EQ(g_l.r[1].left, 110);
    CHECK_EQ(g_l.r[1].right, 150);
    rec_move(&g_l, 0, 0, 120, 480, 100, 0);
    CHECK_EQ(g_l.n, 2);
    rec_init(&g_l, 640, 480);
    rec_add(&g_l, 10, 10, 50, 50);
    rec_move(&g_l, 200, 200, 300, 300, 5, 5);
    CHECK_EQ(g_l.n, 1);
    rec_move(&g_l, 0, 0, 0, 480, 5, 5);
    CHECK_EQ(g_l.n, 1);
    take();
    rec_flag(&g_l, MR_F_ALL);
    CHECK_EQ(rec_move(&g_l, 0, 0, 640, 480, 3, 3), 0);
    take();
    CHECK_EQ(g_o.flags & MR_F_ALL, MR_F_ALL);
}

#define MW 48
#define MH 32

typedef struct {
    int  copy;
    LONG l, t, r, b, dx, dy;
} MODEL_ORDER;

static unsigned char g_sh[MH][MW], g_cl[MH][MW];
static MODEL_ORDER g_mo[64];

static void model_run(unsigned char (*px)[MW], const MODEL_ORDER *o)
{
    static unsigned char tmp[MH][MW];
    LONG x, y;

    memcpy(tmp, px, sizeof(tmp));
    for (y = o->t; y < o->b; y++) {
        for (x = o->l; x < o->r; x++) {
            px[y][x] = o->copy ? tmp[y - o->dy][x - o->dx] :
                       (unsigned char) (tmp[y][x] ^ 0xFF);
        }
    }
}

static void test_move_model(void)
{
    MODEL_ORDER *o;
    LONG round, k, w, h, x, y, v;
    ULONG i;
    int n, bad = 0;

    rec_init(&g_l, MW, MH);
    memset(g_sh, 0, sizeof(g_sh));
    memset(g_cl, 0, sizeof(g_cl));
    for (round = 0; round < 3000 && !bad; round++) {
        n = 0;
        for (k = 1 + rnd(12); k > 0; k--) {
            w = 1 + rnd(MW / 2);
            h = 1 + rnd(MH / 2);
            x = rnd(MW - w + 1);
            y = rnd(MH - h + 1);
            switch (rnd(3)) {
            case 0:
                v = 1 + rnd(250);
                for (i = 0; i < (ULONG) h; i++) {
                    memset(&g_sh[y + i][x], (int) v, (size_t) w);
                }
                rec_add(&g_l, x, y, x + w, y + h);
                break;
            case 1:
                o = &g_mo[n++];
                o->copy = 1;
                o->l = x;
                o->t = y;
                o->r = x + w;
                o->b = y + h;
                o->dx = x - rnd(MW - w + 1);
                o->dy = y - rnd(MH - h + 1);
                model_run(g_sh, o);
                rec_move(&g_l, o->l, o->t, o->r, o->b, o->dx, o->dy);
                break;
            default:
                o = &g_mo[n++];
                o->copy = 0;
                o->l = x;
                o->t = y;
                o->r = x + w;
                o->b = y + h;
                o->dx = o->dy = 0;
                model_run(g_sh, o);
                break;
            }
        }
        for (k = 0; k < n; k++) {
            model_run(g_cl, &g_mo[k]);
        }
        take();
        for (i = 0; i < g_o.nrects; i++) {
            for (y = g_o.rect[i].top; y < g_o.rect[i].bottom; y++) {
                memcpy(&g_cl[y][g_o.rect[i].left], &g_sh[y][g_o.rect[i].left],
                       (size_t) (g_o.rect[i].right - g_o.rect[i].left));
            }
        }
        if (memcmp(g_cl, g_sh, sizeof(g_sh)) != 0) {
            CHECK(0);
            printf("  model round %ld: the client differs from the shadow\n",
                   round);
            bad = 1;
        }
    }
}

static void test_bounds(void)
{
    MR_RECT b;

    rec_init(&g_l, 1280, 1024);
    CHECK(rec_bounds(&g_l, 0, 0, 100, 100, &b) == 0);
    CHECK(same(&b, 0, 0, 0, 0));
    rec_add(&g_l, 10, 10, 20, 20);
    rec_add(&g_l, 300, 300, 400, 400);
    CHECK(rec_bounds(&g_l, 0, 0, 100, 100, &b) == 1);
    CHECK(same(&b, 10, 10, 20, 20));
    CHECK(rec_bounds(&g_l, 15, 0, 350, 310, &b) == 1);
    CHECK(same(&b, 15, 10, 350, 310));
    CHECK(rec_bounds(&g_l, 20, 20, 300, 300, &b) == 0);
    CHECK(rec_bounds(&g_l, 50, 50, 40, 60, &b) == 0);
    rec_flag(&g_l, MR_F_ALL);
    CHECK(rec_bounds(&g_l, 50, 60, 70, 80, &b) == 1);
    CHECK(same(&b, 50, 60, 70, 80));
}

int main(void)
{
    test_bounds();
    test_move();
    test_move_model();
    test_basic();
    test_clip_and_empty();
    test_join();
    test_full();
    test_flags();
    test_random();
    test_flush_basic();
    test_flush_all();
    test_flush_4bpp();
    test_flush_random();
    return t_result("t_record");
}
