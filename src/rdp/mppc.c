/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "mppc.h"

#ifndef MPPC_HASH
#define MPPC_HASH      32768
#endif
#define MPPC_MAX_IN_0  8188
#define MPPC_MAX_IN_1  16365

typedef struct {
    unsigned char *start, *p, *stop;
    unsigned long  acc;
    int            nbits;
    int            overflow;
} BW;

static void bw_init(BW *w, unsigned char *out, unsigned int cap)
{
    w->start = out;
    w->p = out;
    w->stop = out + cap;
    w->acc = 0;
    w->nbits = 0;
    w->overflow = 0;
}

static void put(BW *w, unsigned int value, int count)
{
    if (w->overflow) {
        return;
    }
    w->acc = (w->acc << count) | (unsigned long) value;
    w->nbits += count;
    while (w->nbits >= 8) {
        w->nbits -= 8;
        if (w->p == w->stop) {
            w->overflow = 1;
            return;
        }
        *w->p++ = (unsigned char) (w->acc >> w->nbits);
    }
    w->acc &= (1UL << w->nbits) - 1;
}

static unsigned int bw_finish(BW *w)
{
    if (!w->overflow && w->nbits > 0) {
        if (w->p == w->stop) {
            w->overflow = 1;
        } else {
            *w->p++ = (unsigned char) (w->acc << (8 - w->nbits));
        }
    }
    return (unsigned int) (w->p - w->start);
}

static void put_literal(BW *w, unsigned char b)
{
    if (b < 0x80) {
        put(w, b, 8);
    } else {
        put(w, 0x100 | (b & 0x7F), 9);
    }
}

static void put_length(BW *w, unsigned int len)
{
    int k = 2;

    if (len == 3) {
        put(w, 0, 1);
        return;
    }
    while ((len >> (k + 1)) != 0) {
        k++;
    }
    put(w, (1u << k) - 2, k);
    put(w, len - (1u << k), k);
}

static void put_copy(BW *w, int type, unsigned int off, unsigned int len)
{
    if (type == 0) {
        if (off < 64) {
            put(w, 0x3C0 | off, 10);
        } else if (off < 320) {
            put(w, 0xE00 | (off - 64), 12);
        } else {
            put(w, 0xC000 | (off - 320), 16);
        }
    } else {
        if (off < 64) {
            put(w, 0x7C0 | off, 11);
        } else if (off < 320) {
            put(w, 0x1E00 | (off - 64), 13);
        } else if (off < 2368) {
            put(w, 0x7000 | (off - 320), 15);
        } else {
            put(w, 0x6, 3);
            put(w, off - 2368, 16);
        }
    }
    put_length(w, len);
}

int mppc_copy_bits(int type, unsigned int offset, unsigned int length,
                   unsigned char *out, unsigned int cap)
{
    BW w;
    int bits;

    bw_init(&w, out, cap);
    put_copy(&w, type, offset, length);
    bits = (int) (w.p - w.start) * 8 + w.nbits;
    bw_finish(&w);
    return w.overflow ? -1 : bits;
}

static unsigned int hash3(const unsigned char *p)
{
    return ((((unsigned int) p[0] * 33) + p[1]) * 33 + p[2]) &
           (MPPC_HASH - 1);
}

void mppc_restart(MPPC_ENC *e)
{
    e->pos = 0;
    memset(e->table, 0, MPPC_HASH * sizeof(unsigned short));
    e->pending = MPPC_FLUSHED | MPPC_AT_FRONT;
}

MPPC_ENC *mppc_new(int type)
{
    MPPC_ENC *e;

    e = (MPPC_ENC *) malloc(sizeof(MPPC_ENC));
    if (e == NULL) {
        return NULL;
    }
    memset(e, 0, sizeof(*e));
    e->type = type ? 1 : 0;
    e->hsize = e->type ? 65536u : 8192u;
    e->max_in = e->type ? MPPC_MAX_IN_1 : MPPC_MAX_IN_0;
    e->hist = (unsigned char *) malloc(e->hsize);
    e->table = (unsigned short *) malloc(MPPC_HASH * sizeof(unsigned short));
    e->out = (unsigned char *) malloc(e->max_in);
    if (e->hist == NULL || e->table == NULL || e->out == NULL) {
        mppc_free(e);
        return NULL;
    }
    mppc_restart(e);
    return e;
}

void mppc_free(MPPC_ENC *e)
{
    if (e != NULL) {
        free(e->hist);
        free(e->table);
        free(e->out);
        free(e);
    }
}

unsigned int mppc_compress(MPPC_ENC *e, const unsigned char *src,
                           unsigned int n, const unsigned char **out,
                           unsigned int *outlen)
{
    unsigned char *h = e->hist;
    unsigned int start, end, i, len, slot, flags;
    long cand;
    double t0;
    BW w;

    e->bodies++;
    e->bytes_in += n;
    if (n == 0 || n > e->max_in) {
        e->raw_big++;
        e->bytes_out += n;
        return 0;
    }
    t0 = log_ms();
    if (e->pos + n > e->hsize - 4) {
        mppc_restart(e);
        e->restarts++;
    }
    start = e->pos;
    end = start + n;
    memcpy(h + start, src, n);
    bw_init(&w, e->out, n - 1);

    i = start;
    while (i < end && !w.overflow) {
        len = 0;
        if (end - i >= 3) {
            slot = hash3(h + i);
            cand = (long) e->table[slot] - 1;
            e->table[slot] = (unsigned short) (i + 1);
            if (cand >= 0 && (unsigned long) cand < i &&
                h[cand] == h[i] && h[cand + 1] == h[i + 1] &&
                h[cand + 2] == h[i + 2]) {
                len = 3;
                while (i + len < end && h[cand + len] == h[i + len]) {
                    len++;
                }
            }
        }
        if (len >= 3) {
            put_copy(&w, e->type, i - (unsigned int) cand, len);
            i += len;
        } else {
            put_literal(&w, h[i]);
            i++;
        }
    }
    *outlen = bw_finish(&w);
    e->ms += log_ms() - t0;
    if (w.overflow) {
        e->pos = start;
        e->expanded++;
        e->bytes_out += n;
        return 0;
    }
    e->pos = end;
    flags = MPPC_COMPRESSED | (unsigned int) e->type | e->pending;
    e->pending = 0;
    e->packed++;
    e->bytes_out += *outlen;
    *out = e->out;
    return flags;
}
