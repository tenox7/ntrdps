/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <stdio.h>
#include <string.h>
#include "caps.h"
#include "draw.h"
#include "reduce.h"
#include "rle.h"
#include "update.h"
#include "bitmaps.h"

#define RAW_MAX   (CORE_BITMAP_MAX * CORE_BITMAP_MAX * 3)
#define FILL_HEAD 15
#define CD_HEADER 8
#define MEMBLT_MAX 32

void bitmaps_reset(SESSION *s)
{
    BMP_STATE *b = &s->bmp;
    int c;

    for (c = 0; c < BMP_CACHES; c++) {
        memset(b->cache[c].used, 0, sizeof(b->cache[c].used));
    }
    b->table_sent = 0;
}

void bitmaps_caps(SESSION *s, char *text)
{
    BMP_STATE *b = &s->bmp;
    const unsigned char *p = s->caps.bmc2;
    unsigned long bpp = s->depth == 8 ? 1 : s->depth == 24 ? 3 : 2;
    unsigned long entries;
    unsigned int n = 0;
    int c, len;

    b->rev = 0;
    b->usable = 0;
    for (c = 0; c < BMP_CACHES; c++) {
        b->cache[c].entries = 0;
        b->cache[c].cell = 0;
    }
    if (CAPS_HAVE(&s->caps, CAPS_BMCACHE2)) {
        b->rev = 2;
        for (c = 0; c < BMP_CACHES && c < p[3]; c++) {
            entries = p[4 + 4 * c] | (unsigned long) p[5 + 4 * c] << 8 |
                      (unsigned long) p[6 + 4 * c] << 16 |
                      (unsigned long) (p[7 + 4 * c] & 0x7F) << 24;
            b->cache[c].entries = entries > BMP_SLOTS ? BMP_SLOTS :
                                  (unsigned int) entries;
            b->cache[c].cell = (256UL << (2 * c)) * bpp;
        }
    } else if (CAPS_HAVE(&s->caps, CAPS_BMCACHE)) {
        b->rev = 1;
        for (c = 0; c < BMP_CACHES; c++) {
            entries = s->caps.bmc_entries[c];
            b->cache[c].entries = entries > BMP_SLOTS ? BMP_SLOTS :
                                  (unsigned int) entries;
            b->cache[c].cell = s->caps.bmc_cell_size[c];
        }
    }
    if (b->rev == 0) {
        strcpy(text, "no Bitmap Cache capability set");
        return;
    }
    len = sprintf(text, "bitmap caches (revision %d; slots used x bytes):",
                  b->rev);
    for (c = 0; c < BMP_CACHES; c++) {
        len += sprintf(text + len, " %u x %lu", b->cache[c].entries,
                       b->cache[c].cell);
        if (b->cache[c].entries > 0 && b->cache[c].cell > 0) {
            n++;
        } else {
            b->cache[c].entries = 0;
        }
    }
    b->usable = n > 0 && (s->depth != 8 || s->caps.color_table_cache >= 1);
    if (s->depth == 8) {
        sprintf(text + len, "; color tables %u", s->caps.color_table_cache);
    }
}

static int find(const BMP_CACHE *bc, const CORE_BITMAP *bm)
{
    unsigned int i;

    for (i = 0; i < bc->entries; i++) {
        if (bc->used[i] != 0 && bc->key[i][0] == bm->key[0] &&
            bc->key[i][1] == bm->key[1] && bc->cx[i] == bm->cx &&
            bc->cy[i] == bm->cy) {
            return (int) i;
        }
    }
    return -1;
}

static int victim(const BMP_CACHE *bc, unsigned long clock)
{
    unsigned long age, oldest = 0;
    unsigned int i;
    int v = 0;

    for (i = 0; i < bc->entries; i++) {
        if (bc->used[i] == 0) {
            return (int) i;
        }
        age = clock - bc->used[i];
        if (age >= oldest) {
            oldest = age;
            v = (int) i;
        }
    }
    return v;
}

static int fill_order(SESSION *s, const CORE_FRAME *f, const CORE_BITMAP *bm,
                      int c, int sl, unsigned int w4, unsigned char *out,
                      unsigned int room, int *rle)
{
    unsigned char raw[RAW_MAX], packed[ORDER_PDU_BYTES];
    unsigned int bpp = s->depth == 8 ? 1 : s->depth == 24 ? 3 : 2;
    unsigned int row = w4 * bpp, size = row * (unsigned int) bm->cy;
    unsigned int y, len, max;
    int hdr = !(s->caps.extra_flags & 0x0400), n = 0;
    CORE_FRAME bf;

    bf = *f;
    bf.width = (unsigned int) bm->cx;
    bf.height = (unsigned int) bm->cy;
    bf.bits = bm->bits;
    bf.stride = bm->stride;
    for (y = 0; y < (unsigned int) bm->cy; y++) {
        update_row(raw + y * row, &bf, s->reduce, 0,
                   (unsigned int) bm->cy - 1 - y, w4);
    }
    if (s->caps.bitmap_compression) {
        max = size - 1 - (hdr ? CD_HEADER : 0);
        if (max > sizeof(packed)) {
            max = sizeof(packed);
        }
        n = rle_encode(raw, (int) w4, bm->cy, s->depth, packed, (int) max);
    }
    *rle = n > 0;
    len = n > 0 ? (unsigned int) n + (hdr ? CD_HEADER : 0) : size;
    if (FILL_HEAD + len + MEMBLT_MAX > ORDER_PDU_BYTES) {
        return -1;
    }
    if (n > 0) {
        return (int) order_cache_bitmap(c, (unsigned int) sl, w4,
                                        (unsigned int) bm->cy, s->depth, 1,
                                        hdr, packed, (unsigned int) n, out,
                                        room);
    }
    return (int) order_cache_bitmap(c, (unsigned int) sl, w4,
                                    (unsigned int) bm->cy, s->depth, 0, 0,
                                    raw, size, out, room);
}

int bitmaps_order(SESSION *s, const ORDER_SINK *k, const CORE_FRAME *f,
                  const CORE_ORDER *o, unsigned char *out, unsigned int room,
                  unsigned int *count, int *done)
{
    BMP_STATE *b = &s->bmp;
    BMP_CACHE *bc;
    const CORE_BITMAP *bm;
    unsigned char pal[256][3];
    unsigned int rop = o->rop & 0xFF, bpp, w4, len = 0, n;
    unsigned long bytes;
    long w = o->rect.right - o->rect.left, h = o->rect.bottom - o->rect.top;
    int c, sl, fill, rle;

    *count = 0;
    *done = 0;
    if (w <= 0 || h <= 0) {
        b->nofit++;
        return -1;
    }
    if (!b->usable || !(k->allow & ORD_A_MEMBLT) || !DRAW_USES_S(rop) ||
        DRAW_USES_P(rop) || (s->depth == 8 && rop != 0xCC)) {
        b->notake++;
        return -1;
    }
    bm = s->core->BitmapGet(s->core_ctx, o->bmp, o->bmp_serial);
    if (bm == NULL) {
        b->gone++;
        return -1;
    }
    if (o->src_x < 0 || o->src_y < 0 || o->src_x + w > bm->cx ||
        o->src_y + h > bm->cy) {
        b->nofit++;
        return -1;
    }
    bpp = s->depth == 8 ? 1 : s->depth == 24 ? 3 : 2;
    w4 = ((unsigned int) bm->cx + 3) & ~3U;
    bytes = (unsigned long) w4 * (unsigned long) bm->cy * bpp;
    for (c = 0; c < BMP_CACHES; c++) {
        if (b->cache[c].entries > 0 && b->cache[c].cell >= bytes) {
            break;
        }
    }
    if (c == BMP_CACHES) {
        b->nofit++;
        return -1;
    }
    bc = &b->cache[c];
    sl = find(bc, bm);
    if (s->depth == 8 && !b->table_sent) {
        if (s->reduce != NULL) {
            reduce_palette(pal);
        } else {
            memcpy(pal, f->palette, sizeof(pal));
        }
        n = order_cache_colortable((const unsigned char (*)[3]) pal, out,
                                   room);
        if (n == 0) {
            return 0;
        }
        b->table_sent = 1;
        b->tables++;
        len += n;
        (*count)++;
    }
    if (sl < 0) {
        sl = victim(bc, b->clock);
        fill = fill_order(s, f, bm, c, sl, w4, out + len, room - len, &rle);
        if (fill < 0) {
            if (len > 0) {
                return (int) len;
            }
            b->nofit++;
            return -1;
        }
        if (fill == 0) {
            return (int) len;
        }
        bc->key[sl][0] = bm->key[0];
        bc->key[sl][1] = bm->key[1];
        bc->cx[sl] = (unsigned char) bm->cx;
        bc->cy[sl] = (unsigned char) bm->cy;
        bc->used[sl] = ++b->clock;
        b->fills++;
        b->fill_rle += (unsigned long) rle;
        b->fill_bytes += (unsigned long) fill;
        len += (unsigned int) fill;
        (*count)++;
    } else {
        b->hits++;
    }
    n = order_memblt(&s->ord, c, (unsigned int) sl, &o->rect, o->src_x,
                     o->src_y, rop, out + len, room - len);
    if (n == 0) {
        return (int) len;
    }
    bc->used[sl] = ++b->clock;
    len += n;
    (*count)++;
    *done = 1;
    return (int) len;
}
