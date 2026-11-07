/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <string.h>
#include <stdio.h>
#include "draw.h"
#include "reduce.h"
#include "orders.h"

#define TS_STANDARD           0x01
#define TS_BOUNDS             0x04
#define TS_TYPE_CHANGE        0x08
#define TS_DELTA_COORDINATES  0x10
#define TS_ZERO_BOUNDS_DELTAS 0x20

#define FK_COORD 1
#define FK_U8    2
#define FK_U16   3
#define FK_COLOR 4
#define FK_B7    5
#define FK_S16   6
#define FK_VAR8  7
#define FK_U32   8

typedef struct {
    int                  type;
    int                  slot;
    int                  fields;
    int                  flag_bytes;
    const unsigned char *kinds;
    int                  var;
} ORD_TABLE;

static const unsigned char k_dstblt[] = {
    FK_COORD, FK_COORD, FK_COORD, FK_COORD, FK_U8
};
static const unsigned char k_patblt[] = {
    FK_COORD, FK_COORD, FK_COORD, FK_COORD, FK_U8, FK_COLOR, FK_COLOR,
    FK_U8, FK_U8, FK_U8, FK_U8, FK_B7
};
static const unsigned char k_scrblt[] = {
    FK_COORD, FK_COORD, FK_COORD, FK_COORD, FK_U8, FK_COORD, FK_COORD
};
static const unsigned char k_lineto[] = {
    FK_U16, FK_COORD, FK_COORD, FK_COORD, FK_COORD, FK_COLOR, FK_U8, FK_U8,
    FK_U8, FK_COLOR
};
static const unsigned char k_opaque[] = {
    FK_COORD, FK_COORD, FK_COORD, FK_COORD, FK_U8, FK_U8, FK_U8
};
static const unsigned char k_fastindex[] = {
    FK_U8, FK_U16, FK_COLOR, FK_COLOR, FK_COORD, FK_COORD, FK_COORD,
    FK_COORD, FK_COORD, FK_COORD, FK_COORD, FK_COORD, FK_COORD, FK_COORD,
    FK_VAR8
};
static const unsigned char k_glyphindex[] = {
    FK_U8, FK_U8, FK_U8, FK_U8, FK_COLOR, FK_COLOR, FK_S16, FK_S16, FK_S16,
    FK_S16, FK_S16, FK_S16, FK_S16, FK_S16, FK_U8, FK_U8, FK_U8, FK_U8,
    FK_B7, FK_S16, FK_S16, FK_VAR8
};

static const unsigned char k_savebitmap[] = {
    FK_U32, FK_COORD, FK_COORD, FK_COORD, FK_COORD, FK_U8
};
static const unsigned char k_memblt[] = {
    FK_U16, FK_COORD, FK_COORD, FK_COORD, FK_COORD, FK_U8, FK_COORD,
    FK_COORD, FK_U16
};

static const ORD_TABLE g_tab[ORD_TYPES] = {
    { ORD_DSTBLT,     0,  5, 1, k_dstblt, -1 },
    { ORD_PATBLT,     1, 12, 2, k_patblt, -1 },
    { ORD_SCRBLT,     2,  7, 1, k_scrblt, -1 },
    { ORD_LINETO,     3, 10, 2, k_lineto, -1 },
    { ORD_OPAQUERECT, 4,  7, 1, k_opaque, -1 },
    { ORD_FASTINDEX,  5, 15, 2, k_fastindex, 0 },
    { ORD_GLYPHINDEX, 6, 22, 3, k_glyphindex, 1 },
    { ORD_SAVEBITMAP, 7,  6, 1, k_savebitmap, -1 },
    { ORD_MEMBLT,     8,  9, 2, k_memblt, -1 }
};

static const ORD_TABLE *table_of(int type)
{
    int i;

    for (i = 0; i < ORD_TYPES; i++) {
        if (g_tab[i].type == type) {
            return &g_tab[i];
        }
    }
    return NULL;
}

static long s16(unsigned long x)
{
    x &= 0xFFFFUL;
    return x >= 0x8000UL ? (long) x - 0x10000L : (long) x;
}

static unsigned long keep(int kind, unsigned long v)
{
    switch (kind) {
    case FK_U8:
        return v & 0xFFUL;
    case FK_COLOR:
        return v & 0xFFFFFFUL;
    case FK_U32:
        return v & 0xFFFFFFFFUL;
    }
    return v & 0xFFFFUL;
}

typedef struct {
    unsigned char *p;
    unsigned int   len, room;
} OW;

static void put8(OW *w, unsigned long b)
{
    if (w->len < w->room) {
        w->p[w->len] = (unsigned char) (b & 0xFF);
    }
    w->len++;
}

static void put16(OW *w, unsigned long v)
{
    put8(w, v);
    put8(w, v >> 8);
}

void order_reset(ORDER_ENC *e)
{
    memset(e, 0, sizeof(*e));
    e->last_type = ORD_PATBLT;
    e->fresh = 1;
}

unsigned int order_encode(ORDER_ENC *e, int type, const unsigned long *v,
                          const long *bounds, unsigned char *out,
                          unsigned int room)
{
    return order_encode_var(e, type, v, NULL, 0, bounds, out, room);
}

unsigned int order_encode_var(ORDER_ENC *e, int type, const unsigned long *v,
                              const unsigned char *data, unsigned int len,
                              const long *bounds, unsigned char *out,
                              unsigned int room)
{
    const ORD_TABLE *t = table_of(type);
    unsigned long nv[ORD_VALUES], *pv, flags = 0;
    unsigned int ctrl = TS_STANDARD, desc = 0, j;
    int i, s, kind, deltas = 1, z;
    long d;
    OW w;

    if (t == NULL || (t->var >= 0 && (len > 255 || (len > 0 &&
                                                    data == NULL)))) {
        return 0;
    }
    pv = e->prev[t->slot];
    for (i = 0, s = 0; i < t->fields; i++) {
        kind = t->kinds[i];
        if (kind == FK_VAR8) {
            if (len != e->data_len[t->var] ||
                (len > 0 && memcmp(data, e->data[t->var], len) != 0)) {
                flags |= 1UL << i;
            }
            nv[s] = 0;
            s++;
            continue;
        }
        if (kind == FK_B7) {
            nv[s] = v[s] & 0xFFFFFFFFUL;
            nv[s + 1] = v[s + 1] & 0xFFFFFFUL;
            if (nv[s] != pv[s] || nv[s + 1] != pv[s + 1]) {
                flags |= 1UL << i;
            }
            s += 2;
            continue;
        }
        nv[s] = keep(kind, v[s]);
        if (nv[s] != pv[s]) {
            flags |= 1UL << i;
            if (kind == FK_COORD) {
                d = s16(nv[s]) - s16(pv[s]);
                if (d < -128 || d > 127) {
                    deltas = 0;
                }
            }
        }
        s++;
    }

    if (e->fresh || type != e->last_type) {
        ctrl |= TS_TYPE_CHANGE;
    }
    if (deltas) {
        ctrl |= TS_DELTA_COORDINATES;
    }
    if (bounds != NULL) {
        ctrl |= TS_BOUNDS;
        for (i = 0; i < 4; i++) {
            d = bounds[i] - e->bounds[i];
            if (d == 0) {
                continue;
            }
            desc |= (d >= -128 && d <= 127) ? 0x10U << i : 0x01U << i;
        }
        if (desc == 0) {
            ctrl |= TS_ZERO_BOUNDS_DELTAS;
        }
    }
    z = 0;
    while (z < t->flag_bytes &&
           ((flags >> (8 * (t->flag_bytes - 1 - z))) & 0xFF) == 0) {
        z++;
    }
    ctrl |= (unsigned int) z << 6;

    w.p = out;
    w.len = 0;
    w.room = room;
    put8(&w, ctrl);
    if (ctrl & TS_TYPE_CHANGE) {
        put8(&w, (unsigned long) type);
    }
    for (i = 0; i < t->flag_bytes - z; i++) {
        put8(&w, flags >> (8 * i));
    }
    if (desc != 0) {
        put8(&w, desc);
        for (i = 0; i < 4; i++) {
            if (desc & (0x10U << i)) {
                put8(&w, (unsigned long) (bounds[i] - e->bounds[i]));
            } else if (desc & (0x01U << i)) {
                put16(&w, (unsigned long) bounds[i]);
            }
        }
    }
    for (i = 0, s = 0; i < t->fields; i++) {
        kind = t->kinds[i];
        if (!(flags & (1UL << i))) {
            s += kind == FK_B7 ? 2 : 1;
            continue;
        }
        switch (kind) {
        case FK_COORD:
            if (deltas) {
                put8(&w, (unsigned long) (s16(nv[s]) - s16(pv[s])));
            } else {
                put16(&w, nv[s]);
            }
            break;
        case FK_U8:
            put8(&w, nv[s]);
            break;
        case FK_U16:
        case FK_S16:
            put16(&w, nv[s]);
            break;
        case FK_U32:
            put16(&w, nv[s]);
            put16(&w, nv[s] >> 16);
            break;
        case FK_VAR8:
            put8(&w, len);
            for (j = 0; j < len; j++) {
                put8(&w, data[j]);
            }
            break;
        case FK_COLOR:
            put8(&w, nv[s]);
            put8(&w, nv[s] >> 8);
            put8(&w, nv[s] >> 16);
            break;
        case FK_B7:
            put16(&w, nv[s]);
            put16(&w, nv[s] >> 16);
            put16(&w, nv[s + 1]);
            put8(&w, nv[s + 1] >> 16);
            s++;
            break;
        }
        s++;
    }
    if (w.len > room) {
        return 0;
    }
    memcpy(pv, nv, (size_t) s * sizeof(nv[0]));
    if (t->var >= 0) {
        if (len > 0) {
            memcpy(e->data[t->var], data, len);
        }
        e->data_len[t->var] = len;
    }
    e->last_type = type;
    e->fresh = 0;
    if (bounds != NULL) {
        for (i = 0; i < 4; i++) {
            e->bounds[i] = bounds[i];
        }
    }
    return w.len;
}

unsigned int order_allowed(unsigned int order_flags,
                           const unsigned char *support, int depth)
{
    unsigned int a = 0;

    if (!(order_flags & 0x0002) || (order_flags & 0x0004) ||
        !(order_flags & 0x0008) || (depth == 8 && !(order_flags & 0x0020))) {
        return 0;
    }
    if (support[0]) {
        a |= ORD_A_DSTBLT;
    }
    if (support[1]) {
        a |= ORD_A_PATBLT;
    }
    if (support[2]) {
        a |= ORD_A_SCRBLT;
    }
    if (support[8]) {
        a |= ORD_A_LINETO;
    }
    if (support[10] || support[1]) {
        a |= ORD_A_OPAQUERECT;
    }
    if (support[19]) {
        a |= ORD_A_FASTINDEX;
    }
    if (support[27]) {
        a |= ORD_A_GLYPHINDEX;
    }
    if (support[11]) {
        a |= ORD_A_SAVEBITMAP;
    }
    if (support[3]) {
        a |= ORD_A_MEMBLT;
    }
    return a;
}

void order_allowed_text(unsigned int allow, char *text)
{
    static const char *names[] = {
        "DstBlt", "PatBlt", "ScrBlt", "LineTo", "OpaqueRect", "FastIndex",
        "GlyphIndex", "SaveBitmap", "MemBlt"
    };
    int i;

    text[0] = '\0';
    for (i = 0; i < 9; i++) {
        if (allow & (1U << i)) {
            if (text[0] != '\0') {
                strcat(text, " ");
            }
            strcat(text, names[i]);
        }
    }
    if (text[0] == '\0') {
        strcpy(text, "none");
    }
}

static unsigned long ts_color(const ORDER_SINK *k, unsigned long v)
{
    unsigned char px[4], idx;

    switch (k->depth) {
    case 8:
        if (k->reduce != NULL) {
            px[0] = (unsigned char) v;
            px[1] = (unsigned char) (v >> 8);
            px[2] = (unsigned char) (v >> 16);
            px[3] = (unsigned char) (v >> 24);
            reduce_pixels(&idx, k->reduce, px, 1, k->frame_bpp);
            return idx;
        }
        return v & 0xFFUL;
    case 15:
        return v & 0x7FFFUL;
    case 16:
        return v & 0xFFFFUL;
    }
    return ((v >> 16) & 0xFFUL) | (v & 0xFF00UL) | ((v & 0xFFUL) << 16);
}

static unsigned long frame_mask(unsigned int bpp)
{
    return bpp == 8 ? 0xFFUL : bpp == 16 ? 0xFFFFUL :
           bpp == 24 ? 0xFFFFFFUL : 0xFFFFFFFFUL;
}

static int fill(ORDER_ENC *e, const ORDER_SINK *k, const long *a,
                unsigned long color, unsigned int rop, unsigned char *out,
                unsigned int room, int *type)
{
    unsigned long v[ORD_VALUES], c;

    memset(v, 0, sizeof(v));
    v[0] = (unsigned long) a[0];
    v[1] = (unsigned long) a[1];
    v[2] = (unsigned long) a[2];
    v[3] = (unsigned long) a[3];
    if (DRAW_USES_S(rop) || rop == 0x5F || rop == 0xAA) {
        return -1;
    }
    if (!DRAW_USES_D(rop)) {
        if (!(k->allow & ORD_A_OPAQUERECT)) {
            return -1;
        }
        c = ts_color(k, draw_rop3(rop, color, 0) & frame_mask(k->frame_bpp));
        v[4] = c & 0xFF;
        v[5] = (c >> 8) & 0xFF;
        v[6] = (c >> 16) & 0xFF;
        *type = ORD_OPAQUERECT;
        return (int) order_encode(e, ORD_OPAQUERECT, v, NULL, out, room);
    }
    if (k->depth == 8) {
        if (!k->invert_bw) {
            return -1;
        }
        rop = 0x55;
    }
    v[4] = rop;
    if (!DRAW_USES_P(rop)) {
        if (!(k->allow & ORD_A_DSTBLT)) {
            return -1;
        }
        *type = ORD_DSTBLT;
        return (int) order_encode(e, ORD_DSTBLT, v, NULL, out, room);
    }
    if (!(k->allow & ORD_A_PATBLT)) {
        return -1;
    }
    v[6] = ts_color(k, color);
    *type = ORD_PATBLT;
    return (int) order_encode(e, ORD_PATBLT, v, NULL, out, room);
}

static int pattern(ORDER_ENC *e, const ORDER_SINK *k, const long *a,
                   const CORE_ORDER *o, unsigned char *out,
                   unsigned int room, int *type)
{
    unsigned long v[ORD_VALUES], c0 = o->color, c1 = o->bg;
    unsigned long mask = frame_mask(k->frame_bpp);
    unsigned int rop = o->rop & 0xFF;

    if (DRAW_USES_S(rop) || rop == 0x5F) {
        return -1;
    }
    if (!DRAW_USES_P(rop)) {
        return fill(e, k, a, c0, rop, out, room, type);
    }
    if (!DRAW_USES_D(rop)) {
        c0 = draw_rop3(rop, c0, 0) & mask;
        c1 = draw_rop3(rop, c1, 0) & mask;
        rop = 0xF0;
        if (c0 == c1) {
            return fill(e, k, a, c0, 0xF0, out, room, type);
        }
    } else if (k->reduce != NULL ||
               (k->depth == 8 && !(o->flags & CORE_OF_RGB8))) {
        return -1;
    }
    if (!(k->allow & ORD_A_PATBLT)) {
        return -1;
    }
    memset(v, 0, sizeof(v));
    v[0] = (unsigned long) a[0];
    v[1] = (unsigned long) a[1];
    v[2] = (unsigned long) a[2];
    v[3] = (unsigned long) a[3];
    v[4] = rop;
    v[5] = ts_color(k, c1);
    v[6] = ts_color(k, c0);
    v[7] = (unsigned long) o->pat_x & 7;
    v[8] = ((unsigned long) o->pat_y + (unsigned long) k->brush_dy) & 7;
    v[9] = 3;
    v[10] = o->pat[7];
    v[11] = (unsigned long) o->pat[6] | ((unsigned long) o->pat[5] << 8) |
            ((unsigned long) o->pat[4] << 16) |
            ((unsigned long) o->pat[3] << 24);
    v[12] = (unsigned long) o->pat[2] | ((unsigned long) o->pat[1] << 8) |
            ((unsigned long) o->pat[0] << 16);
    *type = ORD_PATBLT;
    return (int) order_encode(e, ORD_PATBLT, v, NULL, out, room);
}

int order_core(ORDER_ENC *e, const ORDER_SINK *k, const CORE_ORDER *o,
               unsigned char *out, unsigned int room, int *type)
{
    unsigned long v[ORD_VALUES];
    long a[4], b[4];
    unsigned int rop;

    a[0] = o->rect.left;
    a[1] = o->rect.top;
    a[2] = o->rect.right - o->rect.left;
    a[3] = o->rect.bottom - o->rect.top;
    if (a[2] <= 0 || a[3] <= 0 || k->allow == 0) {
        return -1;
    }
    memset(v, 0, sizeof(v));
    switch (o->kind) {
    case CORE_ORDER_COPY:
        if (!(k->allow & ORD_A_SCRBLT)) {
            return -1;
        }
        v[0] = (unsigned long) a[0];
        v[1] = (unsigned long) a[1];
        v[2] = (unsigned long) a[2];
        v[3] = (unsigned long) a[3];
        v[4] = 0xCC;
        v[5] = (unsigned long) (long) o->src_x;
        v[6] = (unsigned long) (long) o->src_y;
        *type = ORD_SCRBLT;
        return (int) order_encode(e, ORD_SCRBLT, v, NULL, out, room);

    case CORE_ORDER_LINE:
        if (o->rop < 1 || o->rop > 16) {
            return -1;
        }
        rop = draw_rop3_of_rop2(o->rop);
        if ((k->allow & ORD_A_LINETO) && (a[2] == 1 || a[3] == 1) &&
            (k->depth != 8 || o->rop == 13)) {
            v[0] = 1;
            v[1] = (unsigned long) a[0];
            v[2] = (unsigned long) a[1];
            b[0] = a[0];
            b[1] = a[1];
            if (a[3] == 1) {
                v[3] = (unsigned long) (a[0] + a[2]);
                v[4] = (unsigned long) a[1];
                b[2] = a[0] + a[2] - 1;
                b[3] = a[1];
            } else {
                v[3] = (unsigned long) a[0];
                v[4] = (unsigned long) (a[1] + a[3]);
                b[2] = a[0];
                b[3] = a[1] + a[3] - 1;
            }
            v[6] = o->rop;
            v[7] = 0;
            v[8] = 1;
            v[9] = ts_color(k, o->color);
            *type = ORD_LINETO;
            return (int) order_encode(e, ORD_LINETO, v, b, out, room);
        }
        return fill(e, k, a, o->color, rop, out, room, type);

    case CORE_ORDER_FILL:
        return fill(e, k, a, o->color, o->rop & 0xFF, out, room, type);

    case CORE_ORDER_PATTERN:
        return pattern(e, k, a, o, out, room, type);
    }
    return -1;
}

int order_reads(const CORE_ORDER *o, RECT *r)
{
    unsigned int rop;

    if (o->kind == CORE_ORDER_TEXT) {
        return 0;
    }
    if (o->kind == CORE_ORDER_COPY) {
        r->left = o->src_x;
        r->top = o->src_y;
        r->right = o->src_x + (o->rect.right - o->rect.left);
        r->bottom = o->src_y + (o->rect.bottom - o->rect.top);
        return 1;
    }
    rop = o->kind == CORE_ORDER_LINE ? draw_rop3_of_rop2(o->rop) :
          (o->rop & 0xFF);
    if (DRAW_USES_D(rop)) {
        *r = o->rect;
        return 1;
    }
    return 0;
}

static int fits_s16(long v)
{
    return v >= -32768L && v <= 32767L;
}

int order_text(ORDER_ENC *e, const ORDER_SINK *k, const ORDER_TEXT *t,
               const long *bounds, unsigned char *out, unsigned int room,
               int *type)
{
    unsigned char data[255];
    unsigned long v[ORD_VALUES];
    unsigned int len = 0;
    long adv;
    int i, fast, op, same;

    fast = (k->allow & ORD_A_FASTINDEX) != 0;
    if ((!fast && !(k->allow & ORD_A_GLYPHINDEX)) || t->n < 1 ||
        t->n > ORD_TEXT_MAX || t->cache < 0 || t->cache > 9) {
        return -1;
    }
    for (i = 0; i < t->n; i++) {
        adv = i == 0 ? 0 : t->x[i] - t->x[i - 1];
        if (adv < 0 || adv > 32767L || t->slot[i] > 253 ||
            len + 4 > sizeof(data)) {
            return -1;
        }
        data[len++] = t->slot[i];
        if (adv < 128) {
            data[len++] = (unsigned char) adv;
        } else {
            data[len++] = 0x80;
            data[len++] = (unsigned char) (adv & 0xFF);
            data[len++] = (unsigned char) (adv >> 8);
        }
    }
    for (i = 0; i < 4; i++) {
        if (!fits_s16(t->bk[i]) || !fits_s16(t->op[i])) {
            return -1;
        }
    }
    if (!fits_s16(t->x[0]) || !fits_s16(t->y) || t->x[0] == -32768L ||
        t->y == -32768L) {
        return -1;
    }
    op = t->op[0] < t->op[2] && t->op[1] < t->op[3];
    same = op && t->op[0] == t->bk[0] && t->op[1] == t->bk[1] &&
           t->op[2] == t->bk[2] && t->op[3] == t->bk[3];
    memset(v, 0, sizeof(v));
    if (fast) {
        if (op && !same && ((t->op[0] == 0 && t->bk[0] != 0) ||
                            (t->op[2] == 0 && t->bk[2] != 0))) {
            return -1;
        }
        v[0] = (unsigned long) t->cache;
        v[1] = 0x0200;
        v[2] = ts_color(k, t->fg);
        v[3] = ts_color(k, t->bg);
        for (i = 0; i < 4; i++) {
            v[4 + i] = (unsigned long) t->bk[i];
        }
        if (same) {
            v[9] = 0x0F;
            v[11] = 0x8000;
        } else if (op) {
            for (i = 0; i < 4; i++) {
                v[8 + i] = (unsigned long) t->op[i];
            }
        }
        v[12] = t->x[0] == t->bk[0] ? 0x8000 : (unsigned long) t->x[0];
        v[13] = t->y == t->bk[1] ? 0x8000 : (unsigned long) t->y;
        *type = ORD_FASTINDEX;
        return (int) order_encode_var(e, ORD_FASTINDEX, v, data, len,
                                      bounds, out, room);
    }
    if (op && t->op[2] - t->op[0] < 2) {
        return -1;
    }
    v[0] = (unsigned long) t->cache;
    v[1] = 0x02;
    v[4] = ts_color(k, t->fg);
    v[5] = ts_color(k, t->bg);
    for (i = 0; i < 4; i++) {
        v[6 + i] = (unsigned long) t->bk[i];
        v[10 + i] = op ? (unsigned long) t->op[i] : 0;
    }
    v[20] = (unsigned long) t->x[0];
    v[21] = (unsigned long) t->y;
    *type = ORD_GLYPHINDEX;
    return (int) order_encode_var(e, ORD_GLYPHINDEX, v, data, len, bounds,
                                  out, room);
}

unsigned int order_cache_glyph(int cache, int n, const unsigned char *slot,
                               const CORE_GLYPH *const *g,
                               unsigned char *out, unsigned int room)
{
    unsigned int size, full, j;
    int i;
    OW w;

    if (n < 1 || n > 255 || cache < 0 || cache > 9) {
        return 0;
    }
    w.p = out;
    w.len = 0;
    w.room = room;
    put8(&w, 0x03);
    put16(&w, 0);
    put16(&w, 0);
    put8(&w, 3);
    put8(&w, (unsigned long) cache);
    put8(&w, (unsigned long) n);
    for (i = 0; i < n; i++) {
        put16(&w, slot[i]);
        put16(&w, (unsigned long) (long) g[i]->ox);
        put16(&w, (unsigned long) (long) g[i]->oy);
        put16(&w, (unsigned long) g[i]->cx);
        put16(&w, (unsigned long) g[i]->cy);
        size = CORE_GLYPH_SIZE(g[i]->cx, g[i]->cy);
        full = ORD_GLYPH_BYTES(g[i]->cx, g[i]->cy);
        for (j = 0; j < full; j++) {
            put8(&w, j < size ? g[i]->bits[j] : 0);
        }
    }
    if (w.len > room) {
        return 0;
    }
    out[1] = (unsigned char) ((w.len - 13) & 0xFF);
    out[2] = (unsigned char) ((w.len - 13) >> 8);
    return w.len;
}

unsigned int order_savebitmap(ORDER_ENC *e, unsigned long pos,
                              const RECT *r, int op, unsigned char *out,
                              unsigned int room)
{
    unsigned long v[ORD_VALUES];

    memset(v, 0, sizeof(v));
    v[0] = pos;
    v[1] = (unsigned long) r->left;
    v[2] = (unsigned long) r->top;
    v[3] = (unsigned long) (r->right - 1);
    v[4] = (unsigned long) (r->bottom - 1);
    v[5] = (unsigned long) op;
    return order_encode(e, ORD_SAVEBITMAP, v, NULL, out, room);
}

unsigned int order_memblt(ORDER_ENC *e, int cache, unsigned int slot,
                          const RECT *r, long sx, long sy, unsigned int rop,
                          unsigned char *out, unsigned int room)
{
    unsigned long v[ORD_VALUES];

    memset(v, 0, sizeof(v));
    v[0] = (unsigned long) cache;
    v[1] = (unsigned long) r->left;
    v[2] = (unsigned long) r->top;
    v[3] = (unsigned long) (r->right - r->left);
    v[4] = (unsigned long) (r->bottom - r->top);
    v[5] = rop & 0xFF;
    v[6] = (unsigned long) sx;
    v[7] = (unsigned long) sy;
    v[8] = slot;
    return order_encode(e, ORD_MEMBLT, v, NULL, out, room);
}

unsigned int order_cache_bitmap(int cache, unsigned int slot, unsigned int w,
                                unsigned int h, int depth, int rle, int hdr,
                                const unsigned char *data, unsigned int len,
                                unsigned char *out, unsigned int room)
{
    unsigned int bpp = depth == 8 ? 1 : depth == 24 ? 3 : 2, j;
    int head = rle && hdr;
    OW w8;

    if (cache < 0 || cache > 2 || w < 1 || w > 255 || (w & 3) != 0 ||
        h < 1 || h > 255 || len + (head ? 8U : 0U) > 0xFFFFU) {
        return 0;
    }
    w8.p = out;
    w8.len = 0;
    w8.room = room;
    put8(&w8, 0x03);
    put16(&w8, 0);
    put16(&w8, rle && !hdr ? 0x0400 : 0);
    put8(&w8, rle ? 2 : 0);
    put8(&w8, (unsigned long) cache);
    put8(&w8, 0);
    put8(&w8, w);
    put8(&w8, h);
    put8(&w8, (unsigned long) depth);
    put16(&w8, len + (head ? 8U : 0U));
    put16(&w8, slot);
    if (head) {
        put16(&w8, 0);
        put16(&w8, len);
        put16(&w8, w * bpp);
        put16(&w8, w * h * bpp);
    }
    for (j = 0; j < len && w8.len < room; j++) {
        put8(&w8, data[j]);
    }
    w8.len += len - j;
    if (w8.len > room) {
        return 0;
    }
    out[1] = (unsigned char) ((w8.len - 13) & 0xFF);
    out[2] = (unsigned char) ((w8.len - 13) >> 8);
    return w8.len;
}

unsigned int order_cache_colortable(const unsigned char pal[256][3],
                                    unsigned char *out, unsigned int room)
{
    unsigned int i;
    OW w;

    if (room < ORD_COLORTABLE_BYTES) {
        return 0;
    }
    w.p = out;
    w.len = 0;
    w.room = room;
    put8(&w, 0x03);
    put16(&w, ORD_COLORTABLE_BYTES - 13);
    put16(&w, 0);
    put8(&w, 1);
    put8(&w, 0);
    put16(&w, 256);
    for (i = 0; i < 256; i++) {
        put8(&w, pal[i][2]);
        put8(&w, pal[i][1]);
        put8(&w, pal[i][0]);
        put8(&w, 0);
    }
    return w.len;
}
