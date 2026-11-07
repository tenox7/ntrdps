/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <string.h>
#include "rle.h"

#define K_BG     0
#define K_FG     1
#define K_COLOR  2
#define K_DITHER 3
#define K_FGBG   4

#define C_BG      0x00
#define C_FG      0x20
#define C_FGBG    0x40
#define C_COLOR   0x60
#define C_IMAGE   0x80
#define C_SETFG   0xC0
#define C_SETFGBG 0xD0
#define C_DITHER  0xE0
#define M_BG      0xF0
#define M_FG      0xF1
#define M_FGBG    0xF2
#define M_COLOR   0xF3
#define M_IMAGE   0xF4
#define M_SETFG   0xF6
#define M_SETFGBG 0xF7
#define M_DITHER  0xF8
#define SPECIAL_1 0xF9
#define SPECIAL_2 0xFA
#define WHITE     0xFD
#define BLACK     0xFE

#define FGBG_SHORT    248
#define SETFGBG_SHORT 120
#define FGBG_ZEROS    16

typedef struct ENC {
    const unsigned char *pix;
    long w;
    long n;
    int bytes;
    unsigned long white;
    unsigned char *out;
    int len;
    int max;
    int full;
    int fg_known;
    unsigned long fg;
    int last_bg;
    long lit;
    long lit_n;
} ENC;

typedef struct CAND {
    int kind;
    long k;
    long save;
    unsigned long f;
} CAND;

static unsigned long pixel(const ENC *e, long p)
{
    const unsigned char *s = e->pix + p * e->bytes;

    if (e->bytes == 1) {
        return s[0];
    }
    if (e->bytes == 2) {
        return s[0] | (unsigned long) s[1] << 8;
    }
    return s[0] | (unsigned long) s[1] << 8 | (unsigned long) s[2] << 16;
}

static unsigned long xpixel(const ENC *e, long p)
{
    if (p < e->w) {
        return pixel(e, p);
    }
    return pixel(e, p) ^ pixel(e, p - e->w);
}

static long run_x(const ENC *e, long p, long end, unsigned long v)
{
    long k = 0;

    while (p + k < end && xpixel(e, p + k) == v) {
        k++;
    }
    return k;
}

static long run_p(const ENC *e, long p, unsigned long v)
{
    long k = 0;

    while (p + k < e->n && pixel(e, p + k) == v) {
        k++;
    }
    return k;
}

static long len_regular(long k)
{
    return k <= 31 ? 1 : k <= 287 ? 2 : 3;
}

static long len_lite(long k)
{
    return k <= 15 ? 1 : k <= 271 ? 2 : 3;
}

static long len_fgbg(long k, long short_max)
{
    if (k % 8 == 0 && k <= short_max) {
        return 1;
    }
    return k <= 256 ? 2 : 3;
}

static void consider(CAND *c, int kind, long k, long save, unsigned long f)
{
    if (c->kind < 0 || save > c->save) {
        c->kind = kind;
        c->k = k;
        c->save = save;
        c->f = f;
    }
}

static void best_order(const ENC *e, long p, CAND *c)
{
    long line_end = p < e->w ? e->w : e->n;
    long k, cost, zeros, last_f;
    unsigned long v, a, b, f;
    int known;

    c->kind = -1;

    if (!e->last_bg || e->lit_n > 0 || p == e->w) {
        k = run_x(e, p, line_end, 0);
        if (k > 0) {
            consider(c, K_BG, k, k * e->bytes - len_regular(k), 0);
        }
    }

    v = xpixel(e, p);
    if (v != 0) {
        k = run_x(e, p, line_end, v);
        if (e->fg_known && e->fg == v) {
            cost = len_regular(k);
        } else {
            cost = len_lite(k) + e->bytes;
        }
        consider(c, K_FG, k, k * e->bytes - cost, v);
    }

    a = pixel(e, p);
    k = run_p(e, p, a);
    consider(c, K_COLOR, k, k * e->bytes - len_regular(k) - e->bytes, 0);

    if (p + 1 < e->n) {
        b = pixel(e, p + 1);
        if (b != a) {
            k = 1;
            while (p + 2 * k + 1 < e->n && pixel(e, p + 2 * k) == a &&
                   pixel(e, p + 2 * k + 1) == b) {
                k++;
            }
            consider(c, K_DITHER, k,
                     2 * k * e->bytes - len_lite(k) - 2 * e->bytes, 0);
        }
    }

    f = 0;
    zeros = 0;
    last_f = 0;
    for (k = 0; p + k < line_end; k++) {
        v = xpixel(e, p + k);
        if (v == 0) {
            if (++zeros == FGBG_ZEROS) {
                break;
            }
        } else {
            if (f == 0) {
                f = v;
            } else if (v != f) {
                break;
            }
            zeros = 0;
            last_f = k + 1;
        }
    }
    if (f != 0) {
        if (zeros == FGBG_ZEROS) {
            k = last_f;
        }
        known = e->fg_known && e->fg == f;
        cost = len_fgbg(k, known ? FGBG_SHORT : SETFGBG_SHORT) + (k + 7) / 8;
        if (!known) {
            cost += e->bytes;
        }
        consider(c, K_FGBG, k, k * e->bytes - cost, f);
    }
}

static void put(ENC *e, unsigned long b)
{
    if (e->len < e->max) {
        e->out[e->len++] = (unsigned char) (b & 0xFF);
    } else {
        e->full = 1;
    }
}

static void put_bytes(ENC *e, const unsigned char *p, long n)
{
    if (n > e->max - e->len) {
        e->full = 1;
        return;
    }
    memcpy(e->out + e->len, p, n);
    e->len += (int) n;
}

static void put_pixel(ENC *e, unsigned long v)
{
    int i;

    for (i = 0; i < e->bytes; i++) {
        put(e, v >> (8 * i));
    }
}

static void put_mega(ENC *e, int mega, long k)
{
    put(e, mega);
    put(e, k);
    put(e, k >> 8);
}

static void put_regular(ENC *e, int code, int mega, long k)
{
    if (k <= 31) {
        put(e, code | k);
    } else if (k <= 287) {
        put(e, code);
        put(e, k - 32);
    } else {
        put_mega(e, mega, k);
    }
}

static void put_lite(ENC *e, int code, int mega, long k)
{
    if (k <= 15) {
        put(e, code | k);
    } else if (k <= 271) {
        put(e, code);
        put(e, k - 16);
    } else {
        put_mega(e, mega, k);
    }
}

static void put_fgbg_header(ENC *e, int code, int mega, long short_max,
                            long k)
{
    if (k % 8 == 0 && k <= short_max) {
        put(e, code | k / 8);
    } else if (k <= 256) {
        put(e, code);
        put(e, k - 1);
    } else {
        put_mega(e, mega, k);
    }
}

static unsigned long fgbg_mask(const ENC *e, long p, long n)
{
    unsigned long m = 0;
    long i;

    for (i = 0; i < n; i++) {
        if (xpixel(e, p + i) != 0) {
            m |= 1UL << i;
        }
    }
    return m;
}

static void put_fgbg(ENC *e, long p, long k, unsigned long f)
{
    unsigned long m;
    long i;

    if (e->fg_known && e->fg == f) {
        m = fgbg_mask(e, p, k < 8 ? k : 8);
        if (k == 8 && (m == 0x03 || m == 0x05)) {
            put(e, m == 0x03 ? SPECIAL_1 : SPECIAL_2);
            return;
        }
        put_fgbg_header(e, C_FGBG, M_FGBG, FGBG_SHORT, k);
    } else {
        put_fgbg_header(e, C_SETFGBG, M_SETFGBG, SETFGBG_SHORT, k);
        put_pixel(e, f);
        e->fg = f;
        e->fg_known = 1;
    }
    for (i = 0; i < k; i += 8) {
        put(e, fgbg_mask(e, p + i, k - i < 8 ? k - i : 8));
    }
}

static void put_literals(ENC *e)
{
    unsigned long v;

    if (e->lit_n == 0) {
        return;
    }
    v = pixel(e, e->lit);
    if (e->lit_n == 1 && v == 0) {
        put(e, BLACK);
    } else if (e->lit_n == 1 && v == e->white) {
        put(e, WHITE);
    } else {
        put_regular(e, C_IMAGE, M_IMAGE, e->lit_n);
        put_bytes(e, e->pix + e->lit * e->bytes, e->lit_n * e->bytes);
    }
    e->lit_n = 0;
    e->last_bg = 0;
}

static long put_order(ENC *e, long p, const CAND *c)
{
    e->last_bg = c->kind == K_BG;
    switch (c->kind) {
    case K_BG:
        put_regular(e, C_BG, M_BG, c->k);
        break;
    case K_FG:
        if (e->fg_known && e->fg == c->f) {
            put_regular(e, C_FG, M_FG, c->k);
        } else {
            put_lite(e, C_SETFG, M_SETFG, c->k);
            put_pixel(e, c->f);
            e->fg = c->f;
            e->fg_known = 1;
        }
        break;
    case K_COLOR:
        put_regular(e, C_COLOR, M_COLOR, c->k);
        put_pixel(e, pixel(e, p));
        break;
    case K_DITHER:
        put_lite(e, C_DITHER, M_DITHER, c->k);
        put_pixel(e, pixel(e, p));
        put_pixel(e, pixel(e, p + 1));
        return 2 * c->k;
    default:
        put_fgbg(e, p, c->k, c->f);
        break;
    }
    return c->k;
}

int rle_encode(const unsigned char *pix, int w, int h, int bpp,
               unsigned char *out, int max)
{
    ENC e;
    CAND c;
    long p;

    if (w < 4 || w % 4 != 0 || h < 1 || (long) w * h > RLE_MAX_PIXELS) {
        return 0;
    }
    memset(&e, 0, sizeof(e));
    switch (bpp) {
    case 8:
        e.bytes = 1;
        e.white = 0xFF;
        break;
    case 15:
        e.bytes = 2;
        break;
    case 16:
        e.bytes = 2;
        e.white = 0xFFFF;
        break;
    case 24:
        e.bytes = 3;
        e.white = 0xFFFFFF;
        break;
    default:
        return 0;
    }
    e.pix = pix;
    e.w = w;
    e.n = (long) w * h;
    e.out = out;
    e.max = max;

    p = 0;
    while (p < e.n && !e.full) {
        best_order(&e, p, &c);
        if (c.save <= 0 || (c.kind != K_BG && c.k < 2)) {
            if (e.lit_n == 0) {
                e.lit = p;
            }
            e.lit_n++;
            p++;
            continue;
        }
        put_literals(&e);
        p += put_order(&e, p, &c);
    }
    put_literals(&e);
    return e.full ? 0 : e.len;
}
