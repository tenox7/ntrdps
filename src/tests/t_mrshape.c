/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include "mrshape.h"
#include "tcheck.h"

static MR_PTROUT      g_s;
static CORE_POINTER   g_p;
static MRSHAPE_FORMAT g_f;
static unsigned char  g_pal[256][3];
static const char    *g_why;

static void shape(unsigned long flags, long cx, long cy, long hx, long hy)
{
    memset(&g_s, 0, sizeof(g_s));
    g_s.flags = MR_P_SHAPE | flags;
    g_s.cx = cx;
    g_s.cy = cy;
    g_s.hotx = hx;
    g_s.hoty = hy;
}

static void format(int depth, unsigned long r, unsigned long g,
                   unsigned long b)
{
    memset(&g_f, 0, sizeof(g_f));
    g_f.depth = depth;
    g_f.red_mask = r;
    g_f.green_mask = g;
    g_f.blue_mask = b;
    g_f.palette = &g_pal[0][0];
}

static int convert(void)
{
    return mrshape_convert(&g_s, &g_f, &g_p, &g_why);
}

static void mask_byte(ULONG *rows, int y, int i, unsigned char v)
{
    ((unsigned char *) &rows[y])[i] = v;
}

static int is_rgb(int x, int y, int r, int g, int b)
{
    const unsigned char *d = &g_p.color[y][x * 3];

    return d[0] == r && d[1] == g && d[2] == b;
}

static void test_refused(void)
{
    format(16, 0xF800, 0x07E0, 0x001F);
    memset(&g_s, 0, sizeof(g_s));
    CHECK(convert() == 0);
    CHECK(g_why[0] != '\0');
    CHECK_EQ(g_p.cb, sizeof(g_p));

    shape(MR_P_NOBITS, 32, 32, 0, 0);
    CHECK(convert() == 0);

    shape(0, 32, 32, 32, 0);
    CHECK(convert() == 0);
    shape(0, 32, 32, 0, -1);
    CHECK(convert() == 0);
    shape(0, 32, 32, 31, 31);
    CHECK(convert() == 1);
    CHECK_EQ(g_p.hot_x, 31);
    CHECK_EQ(g_p.hot_y, 31);

    format(1, 0, 0, 0);
    shape(MR_P_COLOR, 32, 32, 0, 0);
    CHECK(convert() == 0);
    format(8, 0, 0, 0);
    g_f.palette = NULL;
    CHECK(convert() == 0);
}

static void test_hidden(void)
{
    format(16, 0xF800, 0x07E0, 0x001F);
    shape(MR_P_HIDDEN, 32, 32, 0, 0);
    CHECK(convert() == 1);
    CHECK_EQ(g_p.width, 0);
    CHECK_EQ(g_p.height, 0);
    shape(0, 0, 0, 0, 0);
    CHECK(convert() == 1);
    CHECK_EQ(g_p.width, 0);
}

static void test_mono(void)
{
    format(16, 0xF800, 0x07E0, 0x001F);
    shape(0, 20, 2, 1, 1);
    mask_byte(g_s.and_mask, 0, 0, 0x3F);
    mask_byte(g_s.xor_mask, 0, 0, 0x50);
    mask_byte(g_s.and_mask, 0, 2, 0x00);
    mask_byte(g_s.xor_mask, 1, 0, 0x80);
    CHECK(convert() == 1);
    CHECK_EQ(g_p.width, 20);
    CHECK_EQ(g_p.height, 2);
    CHECK_EQ(g_p.hot_x, 1);
    CHECK_EQ(g_p.hot_y, 1);
    CHECK_EQ(g_p.and_mask[0][0], 0x3F);
    CHECK(is_rgb(0, 0, 0, 0, 0));
    CHECK(is_rgb(1, 0, 255, 255, 255));
    CHECK(is_rgb(2, 0, 0, 0, 0));
    CHECK(is_rgb(3, 0, 255, 255, 255));
    CHECK_EQ(g_p.and_mask[0][2], 0x0F);
    CHECK_EQ(g_p.and_mask[0][3], 0xFF);
    CHECK(is_rgb(0, 1, 255, 255, 255));
    CHECK_EQ(g_p.and_mask[1][0], 0x00);
    CHECK_EQ(g_p.and_mask[2][0], 0xFF);
    CHECK_EQ(g_p.and_mask[31][3], 0xFF);
}

static void test_color(void)
{
    int i;

    format(16, 0xF800, 0x07E0, 0x001F);
    shape(MR_P_COLOR, 32, 32, 0, 0);
    g_s.pix[0][0] = 0xF800;
    g_s.pix[0][1] = 0x07E0;
    g_s.pix[0][2] = 0x001F;
    g_s.pix[0][3] = 0x8410;
    g_s.pix[0][4] = 0xFFFF;
    g_s.pix[0][6] = 0x1861;
    mask_byte(g_s.and_mask, 0, 0, 0x07);
    CHECK(convert() == 1);
    CHECK(is_rgb(0, 0, 255, 0, 0));
    CHECK(is_rgb(1, 0, 0, 255, 0));
    CHECK(is_rgb(2, 0, 0, 0, 255));
    CHECK(is_rgb(3, 0, 132, 130, 132));
    CHECK(is_rgb(4, 0, 255, 255, 255));
    CHECK(is_rgb(5, 0, 0, 0, 0));
    CHECK(is_rgb(6, 0, 24, 12, 8));
    CHECK_EQ(g_p.and_mask[0][0], 0x07);

    format(15, 0x7C00, 0x03E0, 0x001F);
    g_s.pix[0][0] = 0x7FFF;
    g_s.pix[0][1] = 0x4210;
    CHECK(convert() == 1);
    CHECK(is_rgb(0, 0, 255, 255, 255));
    CHECK(is_rgb(1, 0, 132, 132, 132));

    for (i = 0; i < 256; i++) {
        g_pal[i][0] = (unsigned char) i;
        g_pal[i][1] = (unsigned char) (255 - i);
        g_pal[i][2] = (unsigned char) (i / 2);
    }
    format(8, 0, 0, 0);
    shape(MR_P_COLOR, 32, 32, 0, 0);
    g_s.pix[0][0] = 5;
    g_s.pix[0][1] = 200;
    CHECK(convert() == 1);
    CHECK(is_rgb(0, 0, 5, 250, 2));
    CHECK(is_rgb(1, 0, 200, 55, 100));

    format(4, 0, 0, 0);
    g_s.pix[0][0] = 0x13;
    CHECK(convert() == 1);
    CHECK(is_rgb(0, 0, 3, 252, 1));

    format(24, 0, 0, 0);
    g_s.pix[0][0] = 0x00112233UL;
    CHECK(convert() == 1);
    CHECK(is_rgb(0, 0, 0x11, 0x22, 0x33));
    format(32, 0, 0, 0);
    g_s.pix[0][0] = 0xFF445566UL;
    CHECK(convert() == 1);
    CHECK(is_rgb(0, 0, 0x44, 0x55, 0x66));
    format(32, 0x0000FF, 0x00FF00, 0xFF0000);
    CHECK(convert() == 1);
    CHECK(is_rgb(0, 0, 0x66, 0x55, 0x44));
}

static void test_cut(void)
{
    format(16, 0xF800, 0x07E0, 0x001F);
    shape(MR_P_CUT, 48, 48, 2, 3);
    CHECK(convert() == 1);
    CHECK_EQ(g_p.width, 32);
    CHECK_EQ(g_p.height, 32);
    CHECK_EQ(g_p.and_mask[31][3], 0x00);
}

int main(void)
{
    test_refused();
    test_hidden();
    test_mono();
    test_color();
    test_cut();
    return t_result("t_mrshape");
}
