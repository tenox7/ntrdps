/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <string.h>
#include "mrshape.h"

static unsigned char channel(unsigned long v, unsigned long m)
{
    int n = 0;

    if (m == 0) {
        return 0;
    }
    while ((m & 1) == 0) {
        m >>= 1;
        v >>= 1;
    }
    v &= m;
    while (m != 0) {
        n++;
        m >>= 1;
    }
    if (n >= 8) {
        return (unsigned char) (v >> (n - 8));
    }
    v <<= 8 - n;
    return (unsigned char) (v | (v >> n));
}

static int rgb_of(unsigned long v, const MRSHAPE_FORMAT *f, unsigned char *d)
{
    const unsigned char *c;

    switch (f->depth) {
    case 4:
    case 8:
        if (f->palette == NULL) {
            return 0;
        }
        c = f->palette + (v & (f->depth == 4 ? 0x0F : 0xFF)) * 3;
        d[0] = c[0];
        d[1] = c[1];
        d[2] = c[2];
        return 1;
    case 15:
    case 16:
        d[0] = channel(v, f->red_mask);
        d[1] = channel(v, f->green_mask);
        d[2] = channel(v, f->blue_mask);
        return 1;
    case 24:
    case 32:
        d[0] = channel(v, f->red_mask != 0 ? f->red_mask : 0xFF0000UL);
        d[1] = channel(v, f->green_mask != 0 ? f->green_mask : 0x00FF00UL);
        d[2] = channel(v, f->blue_mask != 0 ? f->blue_mask : 0x0000FFUL);
        return 1;
    }
    return 0;
}

int mrshape_convert(const MR_PTROUT *s, const MRSHAPE_FORMAT *f,
                    CORE_POINTER *p, const char **why)
{
    const unsigned char *a, *xr;
    unsigned char bit, *d;
    unsigned int w, h, x, y;
    int color;

    memset(p, 0, sizeof(*p));
    p->cb = sizeof(*p);
    *why = "";
    if ((s->flags & MR_P_SHAPE) == 0) {
        *why = "no shape came yet";
        return 0;
    }
    if ((s->flags & MR_P_HIDDEN) != 0 || s->cx <= 0 || s->cy <= 0) {
        return 1;
    }
    if ((s->flags & MR_P_NOBITS) != 0) {
        *why = "the driver could not read its bitmaps";
        return 0;
    }
    if (s->hotx < 0 || s->hoty < 0 || s->hotx >= CORE_POINTER_MAX ||
        s->hoty >= CORE_POINTER_MAX) {
        *why = "the hot spot lies outside the top left 32 x 32 (rdp-4 9.1)";
        return 0;
    }
    color = (s->flags & MR_P_COLOR) != 0;
    w = (unsigned int) (s->cx < CORE_POINTER_MAX ? s->cx : CORE_POINTER_MAX);
    h = (unsigned int) (s->cy < CORE_POINTER_MAX ? s->cy : CORE_POINTER_MAX);
    memset(p->and_mask, 0xFF, sizeof(p->and_mask));
    p->width = w;
    p->height = h;
    p->hot_x = (unsigned int) s->hotx;
    p->hot_y = (unsigned int) s->hoty;
    for (y = 0; y < h; y++) {
        a = (const unsigned char *) &s->and_mask[y];
        xr = (const unsigned char *) &s->xor_mask[y];
        for (x = 0; x < w; x++) {
            bit = (unsigned char) (0x80 >> (x & 7));
            if ((a[x >> 3] & bit) == 0) {
                p->and_mask[y][x >> 3] &= (unsigned char) ~bit;
            }
            d = &p->color[y][x * 3];
            if (color) {
                if (!rgb_of(s->pix[y][x], f, d)) {
                    *why = "the screen's pixel format is not known";
                    return 0;
                }
            } else if ((xr[x >> 3] & bit) != 0) {
                d[0] = d[1] = d[2] = 0xFF;
            }
        }
    }
    return 1;
}
