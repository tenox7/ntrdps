/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <string.h>
#include "reduce.h"

#define CUBE   10
#define GRAYS  226
#define GRAY_N 20

static const unsigned char sys_colors[20][3] = {
    { 0x00, 0x00, 0x00 }, { 0x80, 0x00, 0x00 }, { 0x00, 0x80, 0x00 },
    { 0x80, 0x80, 0x00 }, { 0x00, 0x00, 0x80 }, { 0x80, 0x00, 0x80 },
    { 0x00, 0x80, 0x80 }, { 0xC0, 0xC0, 0xC0 }, { 0xC0, 0xDC, 0xC0 },
    { 0xA6, 0xCA, 0xF0 },
    { 0xFF, 0xFB, 0xF0 }, { 0xA0, 0xA0, 0xA4 }, { 0x80, 0x80, 0x80 },
    { 0xFF, 0x00, 0x00 }, { 0x00, 0xFF, 0x00 }, { 0xFF, 0xFF, 0x00 },
    { 0x00, 0x00, 0xFF }, { 0xFF, 0x00, 0xFF }, { 0x00, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0xFF }
};

static int sys_entry(int k)
{
    return k < 10 ? k : 236 + k;
}

void reduce_palette(unsigned char pal[256][3])
{
    int i, k, v;

    for (k = 0; k < 20; k++) {
        memcpy(pal[sys_entry(k)], sys_colors[k], 3);
    }
    for (i = 0; i < 216; i++) {
        pal[CUBE + i][0] = (unsigned char) (i / 36 * 0x33);
        pal[CUBE + i][1] = (unsigned char) (i / 6 % 6 * 0x33);
        pal[CUBE + i][2] = (unsigned char) (i % 6 * 0x33);
    }
    for (i = 0; i < GRAY_N; i++) {
        v = (255 * (i + 1) + 10) / 21;
        for (k = 0; k < 3; k++) {
            pal[GRAYS + i][k] = (unsigned char) v;
        }
    }
}

static long dist(const unsigned char *c, int r, int g, int b)
{
    long dr = c[0] - r, dg = c[1] - g, db = c[2] - b;

    return dr * dr + dg * dg + db * db;
}

static int nearest(unsigned char pal[256][3], int r, int g, int b)
{
    int best = 0, i, e;
    long d, bd = dist(pal[0], r, g, b);

    for (i = 1; i < 40; i++) {
        e = i < 10 ? i : GRAYS - 10 + i;
        d = dist(pal[e], r, g, b);
        if (d < bd) {
            bd = d;
            best = e;
        }
    }
    e = CUBE + (r * 5 + 127) / 255 * 36 +
        (g * 5 + 127) / 255 * 6 + (b * 5 + 127) / 255;
    return dist(pal[e], r, g, b) < bd ? e : best;
}

static int mask_bits(unsigned long mask, int *shift)
{
    int bits = 0;

    *shift = 0;
    if (mask == 0 || mask > 0xFFFFUL) {
        return 0;
    }
    while ((mask & 1) == 0) {
        mask >>= 1;
        (*shift)++;
    }
    while (mask & 1) {
        mask >>= 1;
        bits++;
    }
    return mask == 0 && bits <= 8 ? bits : 0;
}

static int widen(unsigned long v, int bits)
{
    int x = (int) (v << (8 - bits)), s;

    for (s = bits; s < 8; s *= 2) {
        x |= x >> s;
    }
    return x;
}

void reduce_table(unsigned char *t, const CORE_FRAME *f)
{
    static const unsigned long rgb565[3] = { 0xF800, 0x07E0, 0x001F };
    unsigned char pal[256][3];
    unsigned long mask[3], used, key, sys_key[20];
    int shift[3], bits[3], c, k, ok = f->bpp == 16;

    mask[0] = f->red_mask;
    mask[1] = f->green_mask;
    mask[2] = f->blue_mask;
    for (c = 0; c < 3 && ok; c++) {
        ok = mask_bits(mask[c], &shift[c]) > 0;
    }
    if (!ok || (mask[0] & mask[1]) || (mask[0] & mask[2]) ||
        (mask[1] & mask[2])) {
        memcpy(mask, rgb565, sizeof(mask));
    }
    used = 0;
    for (c = 0; c < 3; c++) {
        bits[c] = mask_bits(mask[c], &shift[c]);
        used |= mask[c];
    }
    for (k = 0; k < 20; k++) {
        sys_key[k] = 0;
        for (c = 0; c < 3; c++) {
            sys_key[k] |= (unsigned long) (sys_colors[k][c] >>
                                           (8 - bits[c])) << shift[c];
        }
    }
    reduce_palette(pal);
    for (key = 0; key < REDUCE_TABLE_SIZE; key++) {
        if ((key & ~used) != 0) {
            t[key] = t[key & used];
            continue;
        }
        for (k = 0; k < 20 && key != sys_key[k]; k++) {
        }
        t[key] = (unsigned char) (k < 20 ? sys_entry(k) :
            nearest(pal, widen((key & mask[0]) >> shift[0], bits[0]),
                    widen((key & mask[1]) >> shift[1], bits[1]),
                    widen((key & mask[2]) >> shift[2], bits[2])));
    }
}

void reduce_pixels(unsigned char *d, const unsigned char *t,
                   const unsigned char *p, unsigned int n,
                   unsigned int bpp)
{
    unsigned int i, step = bpp / 8;

    if (bpp == 16) {
        for (i = 0; i < n; i++, p += 2) {
            d[i] = t[p[0] | (p[1] << 8)];
        }
        return;
    }
    for (i = 0; i < n; i++, p += step) {
        d[i] = t[((p[2] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[0] >> 3)];
    }
}
