/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <string.h>
#include "rledec.h"

static int bpp_bytes(int bpp)
{
    return bpp == 8 ? 1 : bpp == 24 ? 3 : 2;
}

int rledec_header(const unsigned char *in, long len, long *i, long *n)
{
    static const int regular[6] = {
        RLEDEC_BG, RLEDEC_FG, RLEDEC_FGBG, RLEDEC_COLOR, RLEDEC_IMAGE,
        RLEDEC_BAD
    };
    static const int mega[9] = {
        RLEDEC_BG, RLEDEC_FG, RLEDEC_FGBG, RLEDEC_COLOR, RLEDEC_IMAGE,
        RLEDEC_BAD, RLEDEC_SETFG, RLEDEC_SETFGBG, RLEDEC_DITHER
    };
    unsigned int b;
    int kind;

    if (*i >= len) {
        return RLEDEC_BAD;
    }
    b = in[(*i)++];
    if (b < 0xC0) {
        kind = regular[b >> 5];
        *n = b & 0x1F;
        if (*n == 0) {
            if (*i >= len) {
                return RLEDEC_BAD;
            }
            *n = in[(*i)++] + (kind == RLEDEC_FGBG ? 1 : 32);
        } else if (kind == RLEDEC_FGBG) {
            *n *= 8;
        }
        return kind;
    }
    if (b >= 0xF9) {
        *n = b <= 0xFA ? 8 : 1;
        switch (b) {
        case 0xF9: return RLEDEC_SPECIAL_1;
        case 0xFA: return RLEDEC_SPECIAL_2;
        case 0xFD: return RLEDEC_WHITE;
        case 0xFE: return RLEDEC_BLACK;
        default:   return RLEDEC_BAD;
        }
    }
    if (b >= 0xF0) {
        if (*i + 2 > len) {
            return RLEDEC_BAD;
        }
        *n = in[*i] | (long) in[*i + 1] << 8;
        *i += 2;
        return mega[b - 0xF0];
    }
    kind = b < 0xD0 ? RLEDEC_SETFG : b < 0xE0 ? RLEDEC_SETFGBG : RLEDEC_DITHER;
    *n = b & 0x0F;
    if (*n == 0) {
        if (*i >= len) {
            return RLEDEC_BAD;
        }
        *n = in[(*i)++] + (kind == RLEDEC_SETFGBG ? 1 : 16);
    } else if (kind == RLEDEC_SETFGBG) {
        *n *= 8;
    }
    return kind;
}

typedef struct DEC {
    const unsigned char *in;
    long len;
    long i;
    unsigned char *out;
    long w;
    long n;
    long p;
    int bytes;
    int mode;
    int bad;
} DEC;

static unsigned long get_byte(DEC *d)
{
    if (d->i >= d->len) {
        d->bad = 1;
        return 0;
    }
    return d->in[d->i++];
}

static unsigned long get_pixel(DEC *d)
{
    unsigned long v = 0;
    int i;

    for (i = 0; i < d->bytes; i++) {
        v |= get_byte(d) << (8 * i);
    }
    return v;
}

static unsigned long out_pixel(const DEC *d, long p)
{
    unsigned long v = 0;
    int i;

    for (i = 0; i < d->bytes; i++) {
        v |= (unsigned long) d->out[p * d->bytes + i] << (8 * i);
    }
    return v;
}

static void put(DEC *d, unsigned long v)
{
    int i;

    if (d->p >= d->n) {
        d->bad = 1;
        return;
    }
    for (i = 0; i < d->bytes; i++) {
        d->out[d->p * d->bytes + i] = (unsigned char) (v >> (8 * i) & 0xFF);
    }
    d->p++;
}

static void put_x(DEC *d, int order_first, unsigned long x)
{
    int first = d->mode == RLEDEC_MS ? order_first : d->p < d->w;

    if (first) {
        put(d, x);
    } else if (d->p < d->n) {
        put(d, out_pixel(d, d->p - d->w) ^ x);
    } else {
        d->bad = 1;
    }
}

static unsigned long white(int bpp, int mode)
{
    switch (bpp) {
    case 8:  return 0xFF;
    case 15: return mode == RLEDEC_MS ? 0x7FFF : 0xFFFF;
    case 16: return 0xFFFF;
    default: return 0xFFFFFF;
    }
}

int rledec(const unsigned char *in, long len, int w, int h, int bpp,
           int mode, unsigned char *out)
{
    DEC d;
    long n, j;
    int kind;
    int first = 1;
    int insert = 0;
    int last_bg = 0;
    unsigned long fg = white(bpp, mode);
    unsigned long a, b, m = 0;

    memset(&d, 0, sizeof(d));
    d.in = in;
    d.len = len;
    d.out = out;
    d.w = w;
    d.n = (long) w * h;
    d.bytes = bpp_bytes(bpp);
    d.mode = mode;

    while (d.i < d.len && !d.bad) {
        if (first && d.p >= d.w) {
            first = 0;
            insert = 0;
        }
        kind = rledec_header(d.in, d.len, &d.i, &n);
        switch (kind) {
        case RLEDEC_BG:
            if (mode == RLEDEC_MS ? insert : last_bg && d.p != d.w) {
                put_x(&d, first, fg);
                n--;
            }
            for (j = 0; j < n; j++) {
                put_x(&d, first, 0);
            }
            break;
        case RLEDEC_SETFG:
        case RLEDEC_FG:
            if (kind == RLEDEC_SETFG) {
                fg = get_pixel(&d);
            }
            for (j = 0; j < n; j++) {
                put_x(&d, first, fg);
            }
            break;
        case RLEDEC_SETFGBG:
        case RLEDEC_FGBG:
        case RLEDEC_SPECIAL_1:
        case RLEDEC_SPECIAL_2:
            if (kind == RLEDEC_SETFGBG) {
                fg = get_pixel(&d);
            }
            for (j = 0; j < n; j++) {
                if (j % 8 == 0) {
                    m = kind == RLEDEC_SPECIAL_1 ? 0x03 :
                        kind == RLEDEC_SPECIAL_2 ? 0x05 : get_byte(&d);
                }
                put_x(&d, first, (m >> (j % 8) & 1) ? fg : 0);
            }
            break;
        case RLEDEC_COLOR:
            a = get_pixel(&d);
            for (j = 0; j < n; j++) {
                put(&d, a);
            }
            break;
        case RLEDEC_IMAGE:
            for (j = 0; j < n; j++) {
                put(&d, get_pixel(&d));
            }
            break;
        case RLEDEC_DITHER:
            a = get_pixel(&d);
            b = get_pixel(&d);
            for (j = 0; j < n; j++) {
                put(&d, a);
                put(&d, b);
            }
            break;
        case RLEDEC_WHITE:
            put(&d, white(bpp, mode));
            break;
        case RLEDEC_BLACK:
            put(&d, 0);
            break;
        default:
            d.bad = 1;
            break;
        }
        insert = kind == RLEDEC_BG;
        last_bg = kind == RLEDEC_BG;
    }
    return !d.bad && d.p == d.n;
}
