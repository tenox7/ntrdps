/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <string.h>
#include "draw.h"

static void put_pixel(unsigned char *d, unsigned int b, unsigned long v);
static void pattern_rect(unsigned char *top, long stride, unsigned int b,
                         const CORE_ORDER *o);

unsigned int draw_rop3_of_rop2(unsigned int r2)
{
    unsigned int r = (r2 - 1) & 0x0F;

    return (r & 0x03) | ((r & 0x03) << 2) | ((r & 0x0C) << 2) |
           ((r & 0x0C) << 4);
}

unsigned long draw_rop3(unsigned int rop, unsigned long p, unsigned long d)
{
    unsigned long r = 0;

    if (rop & 0x01) {
        r |= ~p & ~d;
    }
    if (rop & 0x02) {
        r |= ~p & d;
    }
    if (rop & 0x10) {
        r |= p & ~d;
    }
    if (rop & 0x20) {
        r |= p & d;
    }
    return r;
}

unsigned long draw_rop3_sd(unsigned int rop, unsigned long s,
                           unsigned long d)
{
    unsigned long r = 0;

    if (rop & 0x01) {
        r |= ~s & ~d;
    }
    if (rop & 0x02) {
        r |= ~s & d;
    }
    if (rop & 0x04) {
        r |= s & ~d;
    }
    if (rop & 0x08) {
        r |= s & d;
    }
    return r;
}

static void fill_row(unsigned char *d, unsigned long n, unsigned int b,
                     unsigned long v)
{
    unsigned long i;

    if (b == 1) {
        memset(d, (int) (v & 0xFF), n);
        return;
    }
    for (i = 0; i < n; i++, d += b) {
        d[0] = (unsigned char) v;
        d[1] = (unsigned char) (v >> 8);
        if (b == 3) {
            d[2] = (unsigned char) (v >> 16);
        }
    }
}

static void copy_rect(unsigned char *top, long stride, unsigned int b,
                      const CORE_ORDER *o)
{
    long x0 = o->rect.left, y0 = o->rect.top;
    long h = o->rect.bottom - y0, y;
    size_t n = (size_t) (o->rect.right - x0) * b;

    if (o->src_y < y0) {
        for (y = h - 1; y >= 0; y--) {
            memmove(top + (y0 + y) * stride + x0 * (long) b,
                    top + (o->src_y + y) * stride + o->src_x * (long) b, n);
        }
    } else {
        for (y = 0; y < h; y++) {
            memmove(top + (y0 + y) * stride + x0 * (long) b,
                    top + (o->src_y + y) * stride + o->src_x * (long) b, n);
        }
    }
}

void draw_order(unsigned char *top, long stride, unsigned int bpp,
                const CORE_ORDER *o)
{
    unsigned int b = bpp / 8, rop;
    long x0 = o->rect.left, y0 = o->rect.top, y;
    long w = o->rect.right - x0, h = o->rect.bottom - y0;
    unsigned long n, i, mask;
    unsigned char pat[3], *d;

    if (w <= 0 || h <= 0 || (b != 1 && b != 2 && b != 3)) {
        return;
    }
    if (o->kind == CORE_ORDER_COPY) {
        copy_rect(top, stride, b, o);
        return;
    }
    if (o->kind == CORE_ORDER_PATTERN) {
        pattern_rect(top, stride, b, o);
        return;
    }
    if (o->kind == CORE_ORDER_LINE) {
        rop = draw_rop3_of_rop2(o->rop);
    } else if (o->kind == CORE_ORDER_FILL) {
        rop = o->rop & 0xFF;
    } else {
        return;
    }
    n = (unsigned long) w * b;
    mask = b == 1 ? 0xFFUL : b == 2 ? 0xFFFFUL : 0xFFFFFFUL;
    pat[0] = (unsigned char) o->color;
    pat[1] = (unsigned char) (o->color >> 8);
    pat[2] = (unsigned char) (o->color >> 16);
    for (y = 0; y < h; y++) {
        d = top + (y0 + y) * stride + x0 * (long) b;
        if (!DRAW_USES_D(rop)) {
            fill_row(d, (unsigned long) w, b,
                     draw_rop3(rop, o->color, 0) & mask);
        } else if (rop == 0x55) {
            for (i = 0; i < n; i++) {
                d[i] = (unsigned char) ~d[i];
            }
        } else if (rop == 0x5A) {
            for (i = 0; i < n; i++) {
                d[i] ^= pat[i % b];
            }
        } else if (rop != 0xAA) {
            for (i = 0; i < n; i++) {
                d[i] = (unsigned char) draw_rop3(rop, pat[i % b], d[i]);
            }
        }
    }
}

static void put_pixel(unsigned char *d, unsigned int b, unsigned long v)
{
    d[0] = (unsigned char) v;
    if (b > 1) {
        d[1] = (unsigned char) (v >> 8);
    }
    if (b > 2) {
        d[2] = (unsigned char) (v >> 16);
    }
}

void draw_text(unsigned char *top, long stride, unsigned int bpp,
               const CORE_ORDER *o, const CORE_GLYPHREF *r,
               const CORE_GLYPH *const *g)
{
    unsigned int b = bpp / 8, row;
    long l, t, rr, bb, x0, y0, x, y;
    const unsigned char *bits;
    int i;

    if (o->rect.left >= o->rect.right || o->rect.top >= o->rect.bottom ||
        (b != 1 && b != 2 && b != 3)) {
        return;
    }
    l = o->op.left > o->rect.left ? o->op.left : o->rect.left;
    t = o->op.top > o->rect.top ? o->op.top : o->rect.top;
    rr = o->op.right < o->rect.right ? o->op.right : o->rect.right;
    bb = o->op.bottom < o->rect.bottom ? o->op.bottom : o->rect.bottom;
    for (y = t; l < rr && y < bb; y++) {
        fill_row(top + y * stride + l * (long) b, (unsigned long) (rr - l), b,
                 o->bg);
    }
    for (i = 0; i < o->nglyph; i++) {
        x0 = (long) r[i].x + g[i]->ox;
        y0 = (long) o->y + g[i]->oy;
        row = ((unsigned int) g[i]->cx + 7) / 8;
        for (y = 0; y < g[i]->cy; y++) {
            if (y0 + y < o->rect.top || y0 + y >= o->rect.bottom) {
                continue;
            }
            bits = g[i]->bits + (unsigned int) y * row;
            for (x = 0; x < g[i]->cx; x++) {
                if (!(bits[x >> 3] & (0x80 >> (x & 7))) ||
                    x0 + x < o->rect.left || x0 + x >= o->rect.right) {
                    continue;
                }
                put_pixel(top + (y0 + y) * stride + (x0 + x) * (long) b, b,
                          o->color);
            }
        }
    }
}

void draw_bitmap(unsigned char *top, long stride, unsigned int bpp,
                 const CORE_ORDER *o, const CORE_BITMAP *bm)
{
    unsigned int b = bpp / 8, rop = o->rop & 0x0F;
    long x0 = o->rect.left, y0 = o->rect.top, y;
    long w = o->rect.right - x0, h = o->rect.bottom - y0;
    size_t n, i;
    unsigned char *d;
    const unsigned char *sp;

    if (w <= 0 || h <= 0 || (b != 1 && b != 2 && b != 3)) {
        return;
    }
    n = (size_t) w * b;
    for (y = 0; y < h; y++) {
        d = top + (y0 + y) * stride + x0 * (long) b;
        sp = bm->bits + (o->src_y + y) * bm->stride +
             (long) o->src_x * (long) b;
        switch (rop) {
        case 0x0C:
            memcpy(d, sp, n);
            break;
        case 0x08:
            for (i = 0; i < n; i++) {
                d[i] &= sp[i];
            }
            break;
        case 0x06:
            for (i = 0; i < n; i++) {
                d[i] ^= sp[i];
            }
            break;
        case 0x0E:
            for (i = 0; i < n; i++) {
                d[i] |= sp[i];
            }
            break;
        default:
            for (i = 0; i < n; i++) {
                d[i] = (unsigned char) draw_rop3_sd(rop, sp[i], d[i]);
            }
            break;
        }
    }
}

int draw_pat_bit(const CORE_ORDER *o, long x, long y)
{
    return (o->pat[(unsigned long) (y - o->pat_y) & 7] >>
            (7 - ((unsigned long) (x - o->pat_x) & 7))) & 1;
}

static unsigned long get_pixel(const unsigned char *d, unsigned int b)
{
    unsigned long v = d[0];

    if (b > 1) {
        v |= (unsigned long) d[1] << 8;
    }
    if (b > 2) {
        v |= (unsigned long) d[2] << 16;
    }
    return v;
}

static unsigned long pixel_mask(unsigned int b)
{
    return b == 1 ? 0xFFUL : b == 2 ? 0xFFFFUL : 0xFFFFFFUL;
}

static void pattern_rect(unsigned char *top, long stride, unsigned int b,
                         const CORE_ORDER *o)
{
    unsigned int rop = o->rop & 0xFF, row, bit;
    unsigned long c[2], mask = pixel_mask(b), v;
    unsigned char *d;
    long x, y;
    int reads = DRAW_USES_D(rop);

    c[0] = o->color & mask;
    c[1] = o->bg & mask;
    for (y = o->rect.top; y < o->rect.bottom; y++) {
        row = o->pat[(unsigned long) (y - o->pat_y) & 7];
        d = top + y * stride + o->rect.left * (long) b;
        for (x = o->rect.left; x < o->rect.right; x++, d += b) {
            bit = (row >> (7 - ((unsigned long) (x - o->pat_x) & 7))) & 1;
            v = reads ? get_pixel(d, b) : 0;
            put_pixel(d, b, draw_rop3(rop, c[bit], v) & mask);
        }
    }
}

typedef struct {
    unsigned int  seen;
    unsigned long u[2];
} PAT_UNDER;

static int under_add(const unsigned char *top, long stride, unsigned int b,
                     const CORE_ORDER *o, const RECT *r, PAT_UNDER *pu)
{
    const unsigned char *d;
    unsigned long v;
    long x, y;
    int bit;

    for (y = r->top; y < r->bottom; y++) {
        d = top + y * stride + r->left * (long) b;
        for (x = r->left; x < r->right; x++, d += b) {
            bit = draw_pat_bit(o, x, y);
            v = get_pixel(d, b);
            if (!(pu->seen & (1U << bit))) {
                pu->seen |= 1U << bit;
                pu->u[bit] = v;
            } else if (pu->u[bit] != v) {
                return 0;
            }
        }
    }
    return 1;
}

static void part_copy(const CORE_ORDER *o, const RECT *r,
                      const PAT_UNDER *pu, unsigned int b, CORE_ORDER *out)
{
    unsigned int rop = o->rop & 0xFF;
    unsigned long mask = pixel_mask(b), c0, c1;

    c0 = draw_rop3(rop, o->color, pu->u[0]) & mask;
    c1 = draw_rop3(rop, o->bg, pu->u[1]) & mask;
    *out = *o;
    out->rect = *r;
    out->rop = 0xF0;
    out->flags = 0;
    if (!(pu->seen & 2U) || (!(pu->seen & 1U)) || c0 == c1) {
        out->kind = CORE_ORDER_FILL;
        out->color = (pu->seen & 1U) ? c0 : c1;
        out->bg = 0;
        memset(out->pat, 0, sizeof(out->pat));
        out->pat_x = out->pat_y = 0;
        return;
    }
    out->color = c0;
    out->bg = c1;
}

static void pat_line(const CORE_ORDER *o, int cols, long i, long j,
                     RECT *r)
{
    *r = o->rect;
    if (cols) {
        r->left = i;
        r->right = j;
    } else {
        r->top = i;
        r->bottom = j;
    }
}

int draw_pat_split(const unsigned char *top, long stride, unsigned int bpp,
                   const CORE_ORDER *o, int bands, CORE_ORDER *out, int max)
{
    unsigned int b = bpp / 8;
    PAT_UNDER pu, t;
    RECT r;
    long i, j, end;
    int cols, good, n = 0;

    if (o->kind != CORE_ORDER_PATTERN || !DRAW_USES_D(o->rop & 0xFF) ||
        (b != 1 && b != 2 && b != 3) || max < 1) {
        return 0;
    }
    memset(&pu, 0, sizeof(pu));
    if (under_add(top, stride, b, o, &o->rect, &pu)) {
        part_copy(o, &o->rect, &pu, b, out);
        return 1;
    }
    if (!bands) {
        return 0;
    }
    cols = o->rect.right - o->rect.left >= o->rect.bottom - o->rect.top;
    i = cols ? o->rect.left : o->rect.top;
    end = cols ? o->rect.right : o->rect.bottom;
    while (i < end) {
        memset(&pu, 0, sizeof(pu));
        pat_line(o, cols, i, i + 1, &r);
        good = under_add(top, stride, b, o, &r, &pu);
        for (j = i + 1; j < end; j++) {
            pat_line(o, cols, j, j + 1, &r);
            if (good) {
                t = pu;
                if (!under_add(top, stride, b, o, &r, &t)) {
                    break;
                }
                pu = t;
            } else {
                memset(&t, 0, sizeof(t));
                if (under_add(top, stride, b, o, &r, &t)) {
                    break;
                }
            }
        }
        if (n >= max) {
            return 0;
        }
        pat_line(o, cols, i, j, &r);
        if (good) {
            part_copy(o, &r, &pu, b, &out[n]);
        } else {
            out[n] = *o;
            out[n].rect = r;
        }
        n++;
        i = j;
    }
    return n;
}

int draw_pat_rgb8(const unsigned char *top, long stride,
                  const CORE_ORDER *o, const unsigned char *pal)
{
    unsigned int rop = o->rop & 0xFF, c, p, r, k;
    const unsigned char *d;
    long x, y;

    for (y = o->rect.top; y < o->rect.bottom; y++) {
        d = top + y * stride + o->rect.left;
        for (x = o->rect.left; x < o->rect.right; x++, d++) {
            c = (unsigned int) ((draw_pat_bit(o, x, y) ? o->bg : o->color) &
                                0xFF);
            p = *d;
            r = (unsigned int) (draw_rop3(rop, c, p) & 0xFF);
            for (k = 0; k < 3; k++) {
                if (pal[r * 3 + k] != (unsigned char)
                    draw_rop3(rop, pal[c * 3 + k], pal[p * 3 + k])) {
                    return 0;
                }
            }
        }
    }
    return 1;
}
