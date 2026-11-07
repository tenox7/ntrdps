/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include "orders.h"
#include "draw.h"
#include "reduce.h"
#include "tcheck.h"

static ORDER_ENC g_e;
static unsigned char g_out[512];
static unsigned int g_len;

static void enc(int type, unsigned long v0, unsigned long v1,
                unsigned long v2, unsigned long v3, unsigned long v4,
                unsigned long v5, unsigned long v6, unsigned long v7,
                unsigned long v8, unsigned long v9, const long *bounds)
{
    unsigned long v[ORD_VALUES];

    memset(v, 0, sizeof(v));
    v[0] = v0;
    v[1] = v1;
    v[2] = v2;
    v[3] = v3;
    v[4] = v4;
    v[5] = v5;
    v[6] = v6;
    v[7] = v7;
    v[8] = v8;
    v[9] = v9;
    g_len = order_encode(&g_e, type, v, bounds, g_out, sizeof(g_out));
}

static void expect(const unsigned char *x, unsigned int n, int line)
{
    if (g_len != n || memcmp(g_out, x, n) != 0) {
        t_failures++;
        printf("t_orders.c(%d): the order has other bytes (%u, want %u)\n",
               line, g_len, n);
    }
}

static void test_sequence_a(void)
{
    static const long clip[4] = { 0, 0, 639, 479 };
    static const long clip2[4] = { 8, 0, 600, 479 };

    order_reset(&g_e);
    enc(ORD_OPAQUERECT, 10, 20, 300, 40, 0x00, 0xF8, 0x00, 0, 0, 0, NULL);
    {
        static const unsigned char a1[] = {
            0x09, 0x0A, 0x2F, 0x0A, 0x00, 0x14, 0x00, 0x2C, 0x01, 0x28,
            0x00, 0xF8
        };
        CHECK_EQ(g_len, sizeof(a1));
        CHECK_MEM(g_out, a1, sizeof(a1));
    }
    enc(ORD_OPAQUERECT, 10, 70, 300, 40, 0x00, 0xF8, 0x00, 0, 0, 0, NULL);
    {
        static const unsigned char a2[] = { 0x11, 0x02, 0x32 };
        CHECK_EQ(g_len, sizeof(a2));
        CHECK_MEM(g_out, a2, sizeof(a2));
    }
    enc(ORD_SCRBLT, 110, 120, 200, 150, 0xCC, 100, 100, 0, 0, 0, clip);
    {
        static const unsigned char a3[] = {
            0x0D, 0x02, 0x7F, 0x0C, 0x7F, 0x02, 0xDF, 0x01, 0x6E, 0x00,
            0x78, 0x00, 0xC8, 0x00, 0x96, 0x00, 0xCC, 0x64, 0x00, 0x64,
            0x00
        };
        CHECK_EQ(g_len, sizeof(a3));
        CHECK_MEM(g_out, a3, sizeof(a3));
    }
    enc(ORD_SCRBLT, 114, 124, 200, 150, 0xCC, 110, 120, 0, 0, 0, clip2);
    {
        static const unsigned char a4[] = {
            0x15, 0x63, 0x50, 0x08, 0xD9, 0x04, 0x04, 0x0A, 0x14
        };
        CHECK_EQ(g_len, sizeof(a4));
        CHECK_MEM(g_out, a4, sizeof(a4));
    }
    enc(ORD_SCRBLT, 118, 128, 200, 150, 0xCC, 114, 124, 0, 0, 0, clip2);
    {
        static const unsigned char a5[] = {
            0x35, 0x63, 0x04, 0x04, 0x04, 0x04
        };
        CHECK_EQ(g_len, sizeof(a5));
        CHECK_MEM(g_out, a5, sizeof(a5));
    }
    enc(ORD_LINETO, 1, 10, 200, 310, 200, 0, 13, 0, 1, 0x00FFFF, NULL);
    {
        static const unsigned char a6[] = {
            0x09, 0x09, 0x5F, 0x03, 0x01, 0x00, 0x0A, 0x00, 0xC8, 0x00,
            0x36, 0x01, 0xC8, 0x00, 0x0D, 0x01, 0xFF, 0xFF, 0x00
        };
        CHECK_EQ(g_len, sizeof(a6));
        CHECK_MEM(g_out, a6, sizeof(a6));
    }
}

static void test_b9(void)
{
    static const unsigned char b9[] = {
        0x19, 0x01, 0x3C, 0x0E, 0x10, 0x10, 0xF0, 0x0F, 0x00, 0x00, 0x03,
        0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55
    };
    unsigned long v[ORD_VALUES];

    order_reset(&g_e);
    memset(v, 0, sizeof(v));
    v[2] = 16;
    v[3] = 16;
    v[4] = 0xF0;
    v[5] = 0x00000F;
    v[9] = 3;
    v[10] = 0xAA;
    v[11] = 0xAA55AA55UL;
    v[12] = 0x55AA55UL;
    g_len = order_encode(&g_e, ORD_PATBLT, v, NULL, g_out, sizeof(g_out));
    CHECK_EQ(g_len, sizeof(b9));
    CHECK_MEM(g_out, b9, sizeof(b9));
}

static void test_edges(void)
{
    static const long b0[4] = { 0, 0, 0, 0 };
    static const long b1[4] = { -128, 127, 128, -129 };
    static const unsigned char x1[] = { 0x19, 0x0A, 0x0C, 0x01, 0x01 };
    static const unsigned char x2[] = { 0x11, 0x01, 0x7F };
    static const unsigned char x3[] = { 0x11, 0x01, 0x80 };
    static const unsigned char x4[] = { 0x01, 0x01, 0x7F, 0x00 };
    static const unsigned char x5[] = { 0x01, 0x01, 0xFE, 0xFF };
    static const unsigned char x6[] = { 0x75 };
    static const unsigned char x7[] = {
        0x55, 0x3C, 0x80, 0x7F, 0x80, 0x00, 0x7F, 0xFF
    };
    static const unsigned char x9[] = { 0x59, 0x09, 0x02, 0x05 };
    static const unsigned char x10[] = { 0x91 };
    unsigned long m1 = (unsigned long) -1L, m2 = (unsigned long) -2L;

    order_reset(&g_e);
    enc(ORD_OPAQUERECT, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, NULL);
    expect(x1, sizeof(x1), __LINE__);
    enc(ORD_OPAQUERECT, 127, 0, 1, 1, 0, 0, 0, 0, 0, 0, NULL);
    expect(x2, sizeof(x2), __LINE__);
    enc(ORD_OPAQUERECT, m1, 0, 1, 1, 0, 0, 0, 0, 0, 0, NULL);
    expect(x3, sizeof(x3), __LINE__);
    enc(ORD_OPAQUERECT, 127, 0, 1, 1, 0, 0, 0, 0, 0, 0, NULL);
    expect(x4, sizeof(x4), __LINE__);
    enc(ORD_OPAQUERECT, m2, 0, 1, 1, 0, 0, 0, 0, 0, 0, NULL);
    expect(x5, sizeof(x5), __LINE__);

    enc(ORD_OPAQUERECT, m2, 0, 1, 1, 0, 0, 0, 0, 0, 0, b0);
    expect(x6, sizeof(x6), __LINE__);
    enc(ORD_OPAQUERECT, m2, 0, 1, 1, 0, 0, 0, 0, 0, 0, b1);
    expect(x7, sizeof(x7), __LINE__);
    enc(ORD_OPAQUERECT, m2, 0, 1, 1, 0, 0, 0, 0, 0, 0, b1);
    expect(x6, sizeof(x6), __LINE__);

    order_reset(&g_e);
    enc(ORD_LINETO, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, NULL);
    expect(x9, sizeof(x9), __LINE__);
    enc(ORD_LINETO, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, NULL);
    expect(x10, sizeof(x10), __LINE__);
}

static void test_no_room(void)
{
    unsigned long v[ORD_VALUES];
    static const unsigned char a1[] = {
        0x09, 0x0A, 0x2F, 0x0A, 0x00, 0x14, 0x00, 0x2C, 0x01, 0x28, 0x00,
        0xF8
    };

    order_reset(&g_e);
    memset(v, 0, sizeof(v));
    v[0] = 10;
    v[1] = 20;
    v[2] = 300;
    v[3] = 40;
    v[5] = 0xF8;
    CHECK_EQ(order_encode(&g_e, ORD_OPAQUERECT, v, NULL, g_out, 11), 0);
    CHECK_EQ(order_encode(&g_e, ORD_OPAQUERECT, v, NULL, g_out, 12), 12);
    CHECK_MEM(g_out, a1, sizeof(a1));
    CHECK_EQ(order_encode(&g_e, 0x55, v, NULL, g_out, 64), 0);
}

static void test_allowed(void)
{
    unsigned char sup[32];
    char text[96];
    unsigned int all = ORD_A_DSTBLT | ORD_A_PATBLT | ORD_A_SCRBLT |
                       ORD_A_LINETO | ORD_A_OPAQUERECT;

    memset(sup, 0, sizeof(sup));
    sup[0] = sup[1] = sup[2] = sup[3] = sup[4] = sup[8] = sup[11] = 1;
    sup[18] = sup[19] = sup[22] = 1;
    all |= ORD_A_FASTINDEX;
    all |= ORD_A_SAVEBITMAP;
    all |= ORD_A_MEMBLT;
    CHECK_EQ(order_allowed(0x002A, sup, 16), all);
    CHECK_EQ(order_allowed(0x002A, sup, 8), all);
    CHECK_EQ(order_allowed(0x000A, sup, 8), 0);
    CHECK_EQ(order_allowed(0x000A, sup, 16), all);
    CHECK_EQ(order_allowed(0x0022, sup, 16), 0);
    CHECK_EQ(order_allowed(0x0028, sup, 16), 0);
    CHECK_EQ(order_allowed(0x002E, sup, 16), 0);

    memset(sup, 0, sizeof(sup));
    sup[0] = sup[2] = sup[8] = sup[10] = 1;
    CHECK_EQ(order_allowed(0x002A, sup, 24),
             ORD_A_DSTBLT | ORD_A_SCRBLT | ORD_A_LINETO | ORD_A_OPAQUERECT);
    sup[10] = 0;
    CHECK_EQ(order_allowed(0x002A, sup, 24),
             ORD_A_DSTBLT | ORD_A_SCRBLT | ORD_A_LINETO);

    order_allowed_text(all, text);
    CHECK(strcmp(text, "DstBlt PatBlt ScrBlt LineTo OpaqueRect FastIndex "
                 "SaveBitmap MemBlt") == 0);
    order_allowed_text(0x1FF, text);
    CHECK(strlen(text) < sizeof(text));
    order_allowed_text(ORD_A_GLYPHINDEX | ORD_A_SCRBLT, text);
    CHECK(strcmp(text, "ScrBlt GlyphIndex") == 0);
    order_allowed_text(0, text);
    CHECK(strcmp(text, "none") == 0);
}

static void test_savebitmap(void)
{
    RECT r;
    static const unsigned char s1[] = {
        0x09, 0x0B, 0x1E, 0x64, 0x00, 0x32, 0x00, 0x2B, 0x01, 0xF9, 0x00
    };
    static const unsigned char s2[] = { 0x11, 0x20, 0x01 };
    static const unsigned char s3[] = {
        0x01, 0x3F, 0xA0, 0x0F, 0x00, 0x00, 0x0A, 0x00, 0x14, 0x00, 0x1D,
        0x00, 0x27, 0x00, 0x00
    };
    static const unsigned char s4[] = { 0x51 };

    order_reset(&g_e);
    r.left = 100;
    r.top = 50;
    r.right = 300;
    r.bottom = 250;
    g_len = order_savebitmap(&g_e, 0, &r, 0, g_out, sizeof(g_out));
    expect(s1, sizeof(s1), __LINE__);
    g_len = order_savebitmap(&g_e, 0, &r, 1, g_out, sizeof(g_out));
    expect(s2, sizeof(s2), __LINE__);
    r.left = 10;
    r.top = 20;
    r.right = 30;
    r.bottom = 40;
    CHECK_EQ(order_savebitmap(&g_e, 4000, &r, 0, g_out, 14), 0);
    g_len = order_savebitmap(&g_e, 4000, &r, 0, g_out, sizeof(g_out));
    expect(s3, sizeof(s3), __LINE__);
    g_len = order_savebitmap(&g_e, 4000, &r, 0, g_out, sizeof(g_out));
    expect(s4, sizeof(s4), __LINE__);

    CHECK_EQ(ORD_SAVE_PELS(200, 200), 200UL * 200);
    CHECK_EQ(ORD_SAVE_PELS(10, 21), 10UL * 40);
    CHECK_EQ(ORD_SAVE_PELS(480, 1), 480UL * 20);
}

static CORE_ORDER mk(int kind, long l, long t, long r, long b,
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

static ORDER_SINK sink(int depth, unsigned int frame_bpp)
{
    ORDER_SINK k;

    k.allow = ORD_A_DSTBLT | ORD_A_PATBLT | ORD_A_SCRBLT | ORD_A_LINETO |
              ORD_A_OPAQUERECT;
    k.depth = depth;
    k.frame_bpp = frame_bpp;
    k.reduce = NULL;
    k.invert_bw = 0;
    k.brush_dy = 0;
    return k;
}

static void same_as(const CORE_ORDER *o, const ORDER_SINK *k, int type,
                    const unsigned long *v, const long *bounds)
{
    ORDER_ENC e2;
    unsigned char want[64];
    unsigned int n;
    int got, t = -1;

    order_reset(&g_e);
    order_reset(&e2);
    got = order_core(&g_e, k, o, g_out, sizeof(g_out), &t);
    n = order_encode(&e2, type, v, bounds, want, sizeof(want));
    CHECK(n > 0);
    CHECK_EQ(got, n);
    CHECK_EQ(t, type);
    if (got > 0 && (unsigned int) got == n) {
        CHECK_MEM(g_out, want, n);
    }
}

static void test_core(void)
{
    ORDER_SINK k = sink(16, 16);
    CORE_ORDER o;
    unsigned long v[ORD_VALUES];
    long bnd[4];
    int t;

    o = mk(CORE_ORDER_FILL, 10, 20, 310, 60, 0xF800, 0xF0);
    memset(v, 0, sizeof(v));
    v[0] = 10; v[1] = 20; v[2] = 300; v[3] = 40; v[5] = 0xF8;
    same_as(&o, &k, ORD_OPAQUERECT, v, NULL);

    k = sink(24, 24);
    o = mk(CORE_ORDER_FILL, 0, 0, 8, 8, 0x112233, 0xF0);
    memset(v, 0, sizeof(v));
    v[2] = 8; v[3] = 8; v[4] = 0x11; v[5] = 0x22; v[6] = 0x33;
    same_as(&o, &k, ORD_OPAQUERECT, v, NULL);
    k = sink(24, 32);
    o.color = 0xFF112233UL;
    same_as(&o, &k, ORD_OPAQUERECT, v, NULL);

    k = sink(8, 8);
    o = mk(CORE_ORDER_FILL, 0, 0, 8, 8, 0x07, 0xF0);
    memset(v, 0, sizeof(v));
    v[2] = 8; v[3] = 8; v[4] = 7;
    same_as(&o, &k, ORD_OPAQUERECT, v, NULL);
    k = sink(15, 16);
    o.color = 0xFC00;
    v[4] = 0x00; v[5] = 0x7C;
    same_as(&o, &k, ORD_OPAQUERECT, v, NULL);

    k = sink(16, 16);
    o = mk(CORE_ORDER_FILL, 0, 0, 8, 8, 0x1234, 0xFF);
    memset(v, 0, sizeof(v));
    v[2] = 8; v[3] = 8; v[4] = 0xFF; v[5] = 0xFF;
    same_as(&o, &k, ORD_OPAQUERECT, v, NULL);
    k = sink(8, 8);
    v[5] = 0;
    same_as(&o, &k, ORD_OPAQUERECT, v, NULL);
    o.rop = 0x00;
    v[4] = 0;
    same_as(&o, &k, ORD_OPAQUERECT, v, NULL);
    o.rop = 0x0F;
    v[4] = 0xCB;
    same_as(&o, &k, ORD_OPAQUERECT, v, NULL);

    k = sink(16, 16);
    o = mk(CORE_ORDER_FILL, 0, 0, 8, 8, 0, 0x55);
    memset(v, 0, sizeof(v));
    v[2] = 8; v[3] = 8; v[4] = 0x55;
    same_as(&o, &k, ORD_DSTBLT, v, NULL);
    k = sink(8, 8);
    CHECK_EQ(order_core(&g_e, &k, &o, g_out, sizeof(g_out), &t), -1);
    k.invert_bw = 1;
    same_as(&o, &k, ORD_DSTBLT, v, NULL);
    o = mk(CORE_ORDER_FILL, 0, 0, 8, 8, 0x0F, 0x5A);
    same_as(&o, &k, ORD_DSTBLT, v, NULL);
    k.invert_bw = 0;
    CHECK_EQ(order_core(&g_e, &k, &o, g_out, sizeof(g_out), &t), -1);
    k.invert_bw = 1;
    k.allow &= ~(unsigned int) ORD_A_DSTBLT;
    CHECK_EQ(order_core(&g_e, &k, &o, g_out, sizeof(g_out), &t), -1);
    o = mk(CORE_ORDER_FILL, 0, 0, 8, 8, 0, 0x55);
    k = sink(16, 16);
    k.allow &= ~(unsigned int) ORD_A_DSTBLT;
    CHECK_EQ(order_core(&g_e, &k, &o, g_out, sizeof(g_out), &t), -1);

    k = sink(16, 16);
    o = mk(CORE_ORDER_FILL, 0, 0, 8, 8, 0x07E0, 0x5A);
    memset(v, 0, sizeof(v));
    v[2] = 8; v[3] = 8; v[4] = 0x5A; v[6] = 0x07E0;
    same_as(&o, &k, ORD_PATBLT, v, NULL);
    k.allow &= ~(unsigned int) ORD_A_PATBLT;
    CHECK_EQ(order_core(&g_e, &k, &o, g_out, sizeof(g_out), &t), -1);

    k = sink(16, 16);
    o = mk(CORE_ORDER_FILL, 0, 0, 8, 8, 0, 0x5F);
    CHECK_EQ(order_core(&g_e, &k, &o, g_out, sizeof(g_out), &t), -1);
    o.rop = 0xCC;
    CHECK_EQ(order_core(&g_e, &k, &o, g_out, sizeof(g_out), &t), -1);
    o.rop = 0xAA;
    CHECK_EQ(order_core(&g_e, &k, &o, g_out, sizeof(g_out), &t), -1);
    o.rop = 0xF0;
    k.allow = 0;
    CHECK_EQ(order_core(&g_e, &k, &o, g_out, sizeof(g_out), &t), -1);
    k = sink(16, 16);
    o.rect.right = 0;
    CHECK_EQ(order_core(&g_e, &k, &o, g_out, sizeof(g_out), &t), -1);

    o = mk(CORE_ORDER_COPY, 10, 20, 110, 70, 0, 0xCC);
    o.src_x = 10;
    o.src_y = 30;
    memset(v, 0, sizeof(v));
    v[0] = 10; v[1] = 20; v[2] = 100; v[3] = 50; v[4] = 0xCC;
    v[5] = 10; v[6] = 30;
    same_as(&o, &k, ORD_SCRBLT, v, NULL);
    k.allow &= ~(unsigned int) ORD_A_SCRBLT;
    CHECK_EQ(order_core(&g_e, &k, &o, g_out, sizeof(g_out), &t), -1);

    k = sink(16, 16);
    o = mk(CORE_ORDER_LINE, 10, 5, 20, 6, 0x001F, 13);
    memset(v, 0, sizeof(v));
    v[0] = 1; v[1] = 10; v[2] = 5; v[3] = 20; v[4] = 5;
    v[6] = 13; v[8] = 1; v[9] = 0x001F;
    bnd[0] = 10; bnd[1] = 5; bnd[2] = 19; bnd[3] = 5;
    same_as(&o, &k, ORD_LINETO, v, bnd);
    o = mk(CORE_ORDER_LINE, 3, 4, 4, 9, 0x001F, 7);
    v[1] = 3; v[2] = 4; v[3] = 3; v[4] = 9; v[6] = 7;
    bnd[0] = 3; bnd[1] = 4; bnd[2] = 3; bnd[3] = 8;
    same_as(&o, &k, ORD_LINETO, v, bnd);
    k.allow &= ~(unsigned int) ORD_A_LINETO;
    memset(v, 0, sizeof(v));
    v[0] = 3; v[1] = 4; v[2] = 1; v[3] = 5; v[4] = 0x5A; v[6] = 0x001F;
    same_as(&o, &k, ORD_PATBLT, v, NULL);
    o.rop = 13;
    memset(v, 0, sizeof(v));
    v[0] = 3; v[1] = 4; v[2] = 1; v[3] = 5; v[4] = 0x1F;
    same_as(&o, &k, ORD_OPAQUERECT, v, NULL);
    k = sink(8, 8);
    o.rop = 7;
    CHECK_EQ(order_core(&g_e, &k, &o, g_out, sizeof(g_out), &t), -1);
    o.rop = 17;
    CHECK_EQ(order_core(&g_e, &k, &o, g_out, sizeof(g_out), &t), -1);
}

static void test_reduce(void)
{
    static unsigned char table[REDUCE_TABLE_SIZE];
    CORE_FRAME f;
    ORDER_SINK k = sink(8, 16);
    CORE_ORDER o;
    unsigned long v[ORD_VALUES];
    unsigned char px[2], idx;

    memset(&f, 0, sizeof(f));
    f.cb = sizeof(f);
    f.bpp = 16;
    f.red_mask = 0xF800;
    f.green_mask = 0x07E0;
    f.blue_mask = 0x001F;
    reduce_table(table, &f);
    k.reduce = table;
    px[0] = 0x00;
    px[1] = 0xF8;
    reduce_pixels(&idx, table, px, 1, 16);
    o = mk(CORE_ORDER_FILL, 0, 0, 4, 4, 0xF800, 0xF0);
    memset(v, 0, sizeof(v));
    v[2] = 4; v[3] = 4; v[4] = idx;
    same_as(&o, &k, ORD_OPAQUERECT, v, NULL);
    CHECK(idx != 0);
}

static void test_reads(void)
{
    CORE_ORDER o;
    RECT r;

    o = mk(CORE_ORDER_COPY, 10, 20, 30, 25, 0, 0xCC);
    o.src_x = 40;
    o.src_y = 1;
    CHECK_EQ(order_reads(&o, &r), 1);
    CHECK_EQ(r.left, 40);
    CHECK_EQ(r.top, 1);
    CHECK_EQ(r.right, 60);
    CHECK_EQ(r.bottom, 6);
    o = mk(CORE_ORDER_FILL, 1, 2, 3, 4, 0, 0xF0);
    CHECK_EQ(order_reads(&o, &r), 0);
    o.rop = 0x55;
    CHECK_EQ(order_reads(&o, &r), 1);
    CHECK_EQ(r.left, 1);
    CHECK_EQ(r.bottom, 4);
    o = mk(CORE_ORDER_LINE, 1, 2, 3, 3, 0, 13);
    CHECK_EQ(order_reads(&o, &r), 0);
    o.rop = 7;
    CHECK_EQ(order_reads(&o, &r), 1);
    o = mk(CORE_ORDER_BITMAP, 1, 2, 3, 4, 0, 0xCC);
    CHECK_EQ(order_reads(&o, &r), 0);
    o.rop = 0x88;
    CHECK_EQ(order_reads(&o, &r), 1);
    CHECK_EQ(r.left, 1);
    CHECK_EQ(r.right, 3);
}

static void test_bitmaps(void)
{
    static const unsigned char b1[] = {
        0x03, 0xFC, 0x03, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x80, 0x00,
        0x00
    };
    static const unsigned char b2[] = {
        0x03, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x02, 0x08,
        0x08, 0x00, 0x00, 0x00, 0x05, 0x06, 0x07, 0x08, 0x01, 0x02, 0x03,
        0x04
    };
    static const unsigned char b3[] = {
        0x59, 0x0D, 0x3E, 0x32, 0x3C, 0x04, 0x02, 0xCC
    };
    static const unsigned char b4[] = {
        0x03, 0x04, 0x00, 0x00, 0x04, 0x02, 0x00, 0x00, 0x04, 0x02, 0x08,
        0x02, 0x00, 0x01, 0x00, 0x68, 0x05
    };
    static const unsigned char b4b[] = {
        0x03, 0x0C, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x04, 0x02, 0x08,
        0x0A, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04, 0x00, 0x08,
        0x00, 0x68, 0x05
    };
    static const unsigned char b5[] = { 0x11, 0x02, 0x01, 0x04, 0x01, 0x00 };
    static const unsigned char rows[] = { 5, 6, 7, 8, 1, 2, 3, 4 };
    static const unsigned char rle[] = { 0x68, 0x05 };
    static unsigned char pal[256][3], big[1100];
    RECT r;

    memset(pal, 0, sizeof(pal));
    pal[1][0] = 0x80;
    pal[2][1] = 0x80;
    pal[255][0] = 0x11;
    pal[255][1] = 0x22;
    pal[255][2] = 0x33;
    CHECK_EQ(order_cache_colortable((const unsigned char (*)[3]) pal, big,
                                    1032), 0);
    CHECK_EQ(order_cache_colortable((const unsigned char (*)[3]) pal, big,
                                    sizeof(big)), 1033);
    CHECK_MEM(big, b1, sizeof(b1));
    CHECK_EQ(big[1029], 0x33);
    CHECK_EQ(big[1030], 0x22);
    CHECK_EQ(big[1031], 0x11);
    CHECK_EQ(big[1032], 0x00);

    order_reset(&g_e);
    CHECK_EQ(order_cache_bitmap(0, 0, 4, 2, 8, 0, 0, rows, 8, g_out, 22), 0);
    g_len = order_cache_bitmap(0, 0, 4, 2, 8, 0, 0, rows, 8, g_out,
                               sizeof(g_out));
    CHECK_EQ(g_len, sizeof(b2));
    CHECK_MEM(g_out, b2, sizeof(b2));
    r.left = 50;
    r.top = 60;
    r.right = 54;
    r.bottom = 62;
    g_len = order_memblt(&g_e, 0, 0, &r, 0, 0, 0xCC, g_out, sizeof(g_out));
    CHECK_EQ(g_len, sizeof(b3));
    CHECK_MEM(g_out, b3, sizeof(b3));
    g_len = order_cache_bitmap(0, 1, 4, 2, 8, 1, 0, rle, 2, g_out,
                               sizeof(g_out));
    CHECK_EQ(g_len, sizeof(b4));
    CHECK_MEM(g_out, b4, sizeof(b4));
    g_len = order_cache_bitmap(0, 1, 4, 2, 8, 1, 1, rle, 2, g_out,
                               sizeof(g_out));
    CHECK_EQ(g_len, sizeof(b4b));
    CHECK_MEM(g_out, b4b, sizeof(b4b));
    r.left = 54;
    r.right = 58;
    CHECK_EQ(order_memblt(&g_e, 0, 1, &r, 0, 0, 0xCC, g_out, 5), 0);
    g_len = order_memblt(&g_e, 0, 1, &r, 0, 0, 0xCC, g_out, sizeof(g_out));
    CHECK_EQ(g_len, sizeof(b5));
    CHECK_MEM(g_out, b5, sizeof(b5));

    g_len = order_cache_bitmap(2, 7, 8, 3, 16, 1, 1, rle, 2, g_out,
                               sizeof(g_out));
    CHECK_EQ(g_len, 25);
    CHECK_EQ(g_out[6], 2);
    CHECK_EQ(g_out[10], 16);
    CHECK_EQ(g_out[13], 7);
    CHECK_EQ(g_out[19], 16);
    CHECK_EQ(g_out[21], 48);
    CHECK_EQ(order_cache_bitmap(0, 0, 6, 2, 8, 0, 0, rows, 8, g_out,
                                sizeof(g_out)), 0);
    CHECK_EQ(order_cache_bitmap(3, 0, 4, 2, 8, 0, 0, rows, 8, g_out,
                                sizeof(g_out)), 0);
    CHECK_EQ(order_cache_bitmap(0, 0, 4, 0, 8, 0, 0, rows, 8, g_out,
                                sizeof(g_out)), 0);
}

static CORE_GLYPH g_h, g_i;

static void make_hi(void)
{
    static const unsigned char h[] = { 0x84, 0x84, 0x84, 0xFC, 0x84, 0x84,
                                       0x84 };
    static const unsigned char i[] = { 0x40, 0x00, 0xC0, 0x40, 0x40, 0x40,
                                       0x40 };

    memset(&g_h, 0, sizeof(g_h));
    g_h.oy = -7;
    g_h.cx = 6;
    g_h.cy = 7;
    memcpy(g_h.bits, h, sizeof(h));
    g_i = g_h;
    g_i.cx = 2;
    memcpy(g_i.bits, i, sizeof(i));
}

static void text_hi(ORDER_TEXT *t)
{
    memset(t, 0, sizeof(*t));
    t->cache = 2;
    t->n = 2;
    t->slot[0] = 0;
    t->slot[1] = 1;
    t->x[0] = 100;
    t->x[1] = 107;
    t->y = 50;
    t->bk[0] = 100;
    t->bk[1] = 43;
    t->bk[2] = 110;
    t->bk[3] = 51;
    memcpy(t->op, t->bk, sizeof(t->op));
    t->fg = 0;
    t->bg = 15;
}

static void test_text(void)
{
    static const unsigned char b6[] = {
        0x03, 0x1F, 0x00, 0x00, 0x00, 0x03, 0x02, 0x02,
        0x00, 0x00, 0x00, 0x00, 0xF9, 0xFF, 0x06, 0x00, 0x07, 0x00,
        0x84, 0x84, 0x84, 0xFC, 0x84, 0x84, 0x84, 0x00,
        0x01, 0x00, 0x00, 0x00, 0xF9, 0xFF, 0x02, 0x00, 0x07, 0x00,
        0x40, 0x00, 0xC0, 0x40, 0x40, 0x40, 0x40, 0x00
    };
    static const unsigned char b7[] = {
        0x19, 0x1B, 0xE3, 0x3F, 0x38, 0x02, 0x02, 0x0F, 0x00, 0x00,
        0x64, 0x00, 0x2B, 0x00, 0x6E, 0x00, 0x33, 0x00,
        0x64, 0x00, 0x2B, 0x00, 0x6E, 0x00, 0x33, 0x00,
        0x64, 0x00, 0x32, 0x00, 0x04, 0x00, 0x00, 0x01, 0x07
    };
    static const unsigned char b8[] = {
        0x09, 0x13, 0xFB, 0x7A, 0x02, 0x00, 0x02, 0x0F, 0x00, 0x00,
        0x64, 0x00, 0x2B, 0x00, 0x6E, 0x00, 0x33, 0x00, 0x0F, 0x00,
        0x00, 0x80, 0x00, 0x80, 0x32, 0x00, 0x04, 0x00, 0x00, 0x01, 0x07
    };
    static const unsigned char bigadv[] = { 0x06, 0x00, 0x00, 0x01, 0x80, 0xC8,
                                         0x00 };
    const CORE_GLYPH *g[2];
    unsigned char slot[2];
    ORDER_SINK k;
    ORDER_TEXT t;
    int type = 0;

    make_hi();
    g[0] = &g_h;
    g[1] = &g_i;
    slot[0] = 0;
    slot[1] = 1;
    g_len = order_cache_glyph(2, 2, slot, g, g_out, sizeof(g_out));
    CHECK_EQ(g_len, sizeof(b6));
    CHECK_MEM(g_out, b6, sizeof(b6));
    CHECK_EQ(order_cache_glyph(2, 2, slot, g, g_out, 43), 0);
    CHECK_EQ(ORD_GLYPH_BYTES(6, 7), 8);
    CHECK_EQ(ORD_GLYPH_BYTES(9, 16), 32);
    CHECK_EQ(ORD_GLYPH_BYTES(8, 13), 16);

    memset(&k, 0, sizeof(k));
    k.depth = 8;
    k.frame_bpp = 8;
    k.allow = ORD_A_GLYPHINDEX;
    order_reset(&g_e);
    text_hi(&t);
    g_len = (unsigned int) order_text(&g_e, &k, &t, NULL, g_out,
                                      sizeof(g_out), &type);
    CHECK_EQ(type, ORD_GLYPHINDEX);
    CHECK_EQ(g_len, sizeof(b7));
    CHECK_MEM(g_out, b7, sizeof(b7));

    k.allow = ORD_A_FASTINDEX | ORD_A_GLYPHINDEX;
    g_len = (unsigned int) order_text(&g_e, &k, &t, NULL, g_out,
                                      sizeof(g_out), &type);
    CHECK_EQ(type, ORD_FASTINDEX);
    CHECK_EQ(g_len, sizeof(b8));
    CHECK_MEM(g_out, b8, sizeof(b8));
    g_len = (unsigned int) order_text(&g_e, &k, &t, NULL, g_out,
                                      sizeof(g_out), &type);
    CHECK_EQ(g_len, 1);
    CHECK_EQ(g_out[0], 0x91);
    t.x[1] = 108;
    CHECK_EQ(order_text(&g_e, &k, &t, NULL, g_out, 3, &type), 0);
    t.x[1] = 107;
    CHECK_EQ(order_text(&g_e, &k, &t, NULL, g_out, sizeof(g_out), &type),
             1);

    t.x[0] = 100;
    t.x[1] = 300;
    t.bk[2] = 310;
    memset(t.op, 0, sizeof(t.op));
    g_len = (unsigned int) order_text(&g_e, &k, &t, NULL, g_out,
                                      sizeof(g_out), &type);
    CHECK(g_len > sizeof(bigadv));
    CHECK_MEM(g_out + g_len - sizeof(bigadv), bigadv, sizeof(bigadv));

    text_hi(&t);
    t.x[1] = 99;
    CHECK_EQ(order_text(&g_e, &k, &t, NULL, g_out, sizeof(g_out), &type),
             -1);
    t.x[1] = 100 + 32768L;
    CHECK_EQ(order_text(&g_e, &k, &t, NULL, g_out, sizeof(g_out), &type),
             -1);
    t.x[1] = 107;
    t.slot[1] = 254;
    CHECK_EQ(order_text(&g_e, &k, &t, NULL, g_out, sizeof(g_out), &type),
             -1);
    t.slot[1] = 1;
    k.allow = ORD_A_OPAQUERECT | ORD_A_PATBLT;
    CHECK_EQ(order_text(&g_e, &k, &t, NULL, g_out, sizeof(g_out), &type),
             -1);
    k.allow = ORD_A_GLYPHINDEX;
    t.op[2] = t.op[0] + 1;
    CHECK_EQ(order_text(&g_e, &k, &t, NULL, g_out, sizeof(g_out), &type),
             -1);
    t.op[2] = t.op[0] + 2;
    CHECK(order_text(&g_e, &k, &t, NULL, g_out, sizeof(g_out), &type) > 0);

    {
        unsigned char sup[32];

        memset(sup, 0, sizeof(sup));
        sup[19] = 1;
        CHECK_EQ(order_allowed(0x2A, sup, 16), ORD_A_FASTINDEX);
        sup[19] = 0;
        sup[27] = 1;
        CHECK_EQ(order_allowed(0x2A, sup, 16), ORD_A_GLYPHINDEX);
    }
}

static const unsigned char g_odd[8] = {
    0x80, 0x41, 0x22, 0x14, 0x0F, 0xF0, 0x3C, 0x01
};

static CORE_ORDER mk_pat(unsigned long c0, unsigned long c1,
                         unsigned int rop)
{
    CORE_ORDER o = mk(CORE_ORDER_PATTERN, 4, 5, 104, 9, c0, rop);

    o.bg = c1;
    o.pat_x = 3;
    o.pat_y = 6;
    memcpy(o.pat, g_odd, 8);
    return o;
}

static void pat_values(unsigned long *v, unsigned int rop, unsigned long fore,
                       unsigned long back, unsigned long orgy)
{
    memset(v, 0, ORD_VALUES * sizeof(v[0]));
    v[0] = 4;
    v[1] = 5;
    v[2] = 100;
    v[3] = 4;
    v[4] = rop;
    v[5] = back;
    v[6] = fore;
    v[7] = 3;
    v[8] = orgy;
    v[9] = 3;
    v[10] = 0x01;
    v[11] = 0x3CUL | 0xF0UL << 8 | 0x0FUL << 16 | 0x14UL << 24;
    v[12] = 0x22UL | 0x41UL << 8 | 0x80UL << 16;
}

static void test_patterns(void)
{
    static unsigned char table[REDUCE_TABLE_SIZE];
    CORE_FRAME f;
    ORDER_SINK k = sink(16, 16);
    CORE_ORDER o;
    unsigned long v[ORD_VALUES];
    RECT r;
    int t;

    o = mk_pat(0x0000, 0xFFFF, 0x5A);
    pat_values(v, 0x5A, 0x0000, 0xFFFF, 6);
    same_as(&o, &k, ORD_PATBLT, v, NULL);
    k.brush_dy = 7;
    pat_values(v, 0x5A, 0x0000, 0xFFFF, 5);
    same_as(&o, &k, ORD_PATBLT, v, NULL);
    o.pat_y = 0;
    pat_values(v, 0x5A, 0x0000, 0xFFFF, 7);
    same_as(&o, &k, ORD_PATBLT, v, NULL);
    k.brush_dy = 0;
    k = sink(24, 24);
    o = mk_pat(0x112233, 0x445566, 0xA0);
    pat_values(v, 0xA0, 0x332211, 0x665544, 6);
    same_as(&o, &k, ORD_PATBLT, v, NULL);

    k = sink(8, 8);
    o = mk_pat(0, 15, 0x5A);
    CHECK_EQ(order_core(&g_e, &k, &o, g_out, sizeof(g_out), &t), -1);
    o.flags = CORE_OF_RGB8;
    pat_values(v, 0x5A, 0, 15, 6);
    same_as(&o, &k, ORD_PATBLT, v, NULL);
    memset(&f, 0, sizeof(f));
    f.cb = sizeof(f);
    f.bpp = 16;
    f.red_mask = 0xF800;
    f.green_mask = 0x07E0;
    f.blue_mask = 0x001F;
    reduce_table(table, &f);
    k = sink(8, 16);
    k.reduce = table;
    CHECK_EQ(order_core(&g_e, &k, &o, g_out, sizeof(g_out), &t), -1);

    k = sink(8, 8);
    o = mk_pat(0x06, 0x09, 0x0F);
    pat_values(v, 0xF0, 0xF9, 0xF6, 6);
    same_as(&o, &k, ORD_PATBLT, v, NULL);
    o = mk_pat(0x06, 0x09, 0xFF);
    memset(v, 0, sizeof(v));
    v[0] = 4; v[1] = 5; v[2] = 100; v[3] = 4; v[4] = 0xFF;
    same_as(&o, &k, ORD_OPAQUERECT, v, NULL);
    o = mk_pat(0x06, 0x09, 0xF0);
    k.allow &= ~(unsigned int) ORD_A_PATBLT;
    CHECK_EQ(order_core(&g_e, &k, &o, g_out, sizeof(g_out), &t), -1);

    o = mk_pat(0, 15, 0xF0);
    CHECK_EQ(order_reads(&o, &r), 0);
    o.rop = 0x5A;
    CHECK_EQ(order_reads(&o, &r), 1);
    CHECK_EQ(r.left, 4);
    CHECK_EQ(r.bottom, 9);
}

int main(void)
{
    test_sequence_a();
    test_b9();
    test_text();
    test_savebitmap();
    test_edges();
    test_no_room();
    test_allowed();
    test_core();
    test_reduce();
    test_reads();
    test_bitmaps();
    test_patterns();
    return t_result("t_orders");
}
