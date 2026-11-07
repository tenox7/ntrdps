/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <string.h>
#include "reduce.h"
#include "tcheck.h"

#define MODEL_PALETTE 0xC1AA0B42UL
#define MODEL_565     0x3ACEE193UL
#define MODEL_555     0xA1555F60UL
#define MODEL_BGR     0xEE70CE26UL

static unsigned char g_t[REDUCE_TABLE_SIZE];
static unsigned char g_565[REDUCE_TABLE_SIZE];

static unsigned long crc32(const unsigned char *p, unsigned long n)
{
    unsigned long crc = 0xFFFFFFFFUL;
    int k;

    while (n-- > 0) {
        crc ^= *p++;
        for (k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

static void frame(CORE_FRAME *f, unsigned int bpp, unsigned long red,
                  unsigned long green, unsigned long blue)
{
    memset(f, 0, sizeof(*f));
    f->cb = sizeof(*f);
    f->bpp = bpp;
    f->red_mask = red;
    f->green_mask = green;
    f->blue_mask = blue;
}

static void check_color(unsigned char pal[256][3], int e, unsigned long rgb)
{
    CHECK_EQ(((unsigned long) pal[e][0] << 16) | (pal[e][1] << 8) |
             pal[e][2], rgb);
}

static void test_palette(void)
{
    static const unsigned long sys[20] = {
        0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080,
        0x008080, 0xC0C0C0, 0xC0DCC0, 0xA6CAF0,
        0xFFFBF0, 0xA0A0A4, 0x808080, 0xFF0000, 0x00FF00, 0xFFFF00,
        0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF
    };
    static const int grays[20] = {
        12, 24, 36, 49, 61, 73, 85, 97, 109, 121,
        134, 146, 158, 170, 182, 194, 206, 219, 231, 243
    };
    unsigned char pal[256][3];
    int i;

    memset(pal, 0xEE, sizeof(pal));
    reduce_palette(pal);
    for (i = 0; i < 10; i++) {
        check_color(pal, i, sys[i]);
        check_color(pal, 246 + i, sys[10 + i]);
    }
    check_color(pal, 10, 0x000000);
    check_color(pal, 11, 0x000033);
    check_color(pal, 16, 0x003300);
    check_color(pal, 46, 0x330000);
    check_color(pal, 61, 0x336699);
    check_color(pal, 225, 0xFFFFFF);
    for (i = 0; i < 20; i++) {
        check_color(pal, 226 + i, grays[i] * 0x010101UL);
    }
    CHECK_EQ(crc32(&pal[0][0], sizeof(pal)), MODEL_PALETTE);
}

static void test_565(void)
{
    CORE_FRAME f;

    frame(&f, 16, 0xF800, 0x07E0, 0x001F);
    memset(g_t, 0xEE, sizeof(g_t));
    reduce_table(g_t, &f);
    CHECK_EQ(g_t[0x0000], 0);
    CHECK_EQ(g_t[0xC618], 7);
    CHECK_EQ(g_t[0xC617], 241);
    CHECK_EQ(g_t[0x8410], 248);
    CHECK_EQ(g_t[0xA65E], 9);
    CHECK_EQ(g_t[0xFFDE], 246);
    CHECK_EQ(g_t[0xFFFF], 255);
    CHECK_EQ(g_t[0x0010], 4);
    CHECK_EQ(g_t[0x3333], 61);
    CHECK_EQ(g_t[0x7BCF], 235);
    CHECK_EQ(crc32(g_t, sizeof(g_t)), MODEL_565);
    memcpy(g_565, g_t, sizeof(g_565));
}

static void test_555(void)
{
    CORE_FRAME f;

    frame(&f, 16, 0x7C00, 0x03E0, 0x001F);
    reduce_table(g_t, &f);
    CHECK_EQ(g_t[0x6318], 7);
    CHECK_EQ(g_t[0xE318], 7);
    CHECK_EQ(g_t[0x7FFF], 255);
    CHECK_EQ(g_t[0xFFFF], 255);
    CHECK_EQ(crc32(g_t, sizeof(g_t)), MODEL_555);

    frame(&f, 16, 0x001F, 0x07E0, 0xF800);
    reduce_table(g_t, &f);
    CHECK_EQ(g_t[0x001F], 249);
    CHECK_EQ(g_t[0xF800], 252);
    CHECK_EQ(crc32(g_t, sizeof(g_t)), MODEL_BGR);
}

static void test_other(void)
{
    CORE_FRAME f;

    frame(&f, 24, 0, 0, 0);
    reduce_table(g_t, &f);
    CHECK_MEM(g_t, g_565, sizeof(g_t));
    frame(&f, 32, 0xFF0000, 0x00FF00, 0x0000FF);
    reduce_table(g_t, &f);
    CHECK_MEM(g_t, g_565, sizeof(g_t));
    frame(&f, 16, 0, 0, 0);
    reduce_table(g_t, &f);
    CHECK_MEM(g_t, g_565, sizeof(g_t));
    frame(&f, 16, 0xF00F, 0x07E0, 0x001F);
    reduce_table(g_t, &f);
    CHECK_MEM(g_t, g_565, sizeof(g_t));
    frame(&f, 16, 0xF800, 0x0FE0, 0x001F);
    reduce_table(g_t, &f);
    CHECK_MEM(g_t, g_565, sizeof(g_t));
    frame(&f, 16, 0x1F0000, 0x07E0, 0x001F);
    reduce_table(g_t, &f);
    CHECK_MEM(g_t, g_565, sizeof(g_t));
}

static void test_pixels(void)
{
    static const unsigned char p16[8] = {
        0x00, 0x00, 0x18, 0xC6, 0xFF, 0xFF, 0x1F, 0x00
    };
    static const unsigned char p24[12] = {
        0x00, 0x00, 0x00, 0xC3, 0xC2, 0xC5, 0xFF, 0xFF, 0xFF,
        0xF0, 0xCA, 0xA6
    };
    static const unsigned char p32[16] = {
        0x00, 0x00, 0x00, 0x55, 0xC3, 0xC2, 0xC5, 0xAA, 0xFF, 0xFF, 0xFF,
        0x00, 0xF0, 0xCA, 0xA6, 0xFF
    };
    static const unsigned char want16[4] = { 0, 7, 255, 252 };
    static const unsigned char want24[4] = { 0, 7, 255, 9 };
    unsigned char d[5];

    memset(d, 0xEE, sizeof(d));
    reduce_pixels(d, g_565, p16, 4, 16);
    CHECK_MEM(d, want16, 4);
    CHECK_EQ(d[4], 0xEE);
    reduce_pixels(d, g_565, p24, 4, 24);
    CHECK_MEM(d, want24, 4);
    reduce_pixels(d, g_565, p32, 4, 32);
    CHECK_MEM(d, want24, 4);
    CHECK_EQ(d[4], 0xEE);
}

int main(void)
{
    test_palette();
    test_565();
    test_555();
    test_other();
    test_pixels();
    return t_result("t_reduce");
}
