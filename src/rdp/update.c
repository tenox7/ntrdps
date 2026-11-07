/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "stream.h"
#include "session.h"
#include "mcs.h"
#include "share.h"
#include "rdp.h"
#include "caps.h"
#include "rle.h"
#include "reduce.h"
#include "update.h"
#include "glyphs.h"
#include "bitmaps.h"

#define UPDATE_BITMAP  1
#define UPDATE_PALETTE 2

#define CELL_H_MAX 64
#define CD_HEADER  8
#define RAW_MAX    (UPDATE_TILE_BYTES - UPDATE_TILE_HEADER)

static const char *const ord_names[ORD_TYPES] = {
    "DstBlt", "PatBlt", "ScrBlt", "LineTo", "OpaqueRect", "FastIndex",
    "GlyphIndex", "SaveBitmap", "MemBlt"
};

static int ord_slot(int type)
{
    switch (type) {
    case ORD_DSTBLT:     return 0;
    case ORD_PATBLT:     return 1;
    case ORD_SCRBLT:     return 2;
    case ORD_LINETO:     return 3;
    case ORD_FASTINDEX:  return 5;
    case ORD_GLYPHINDEX: return 6;
    case ORD_SAVEBITMAP: return 7;
    case ORD_MEMBLT:     return 8;
    }
    return 4;
}

static unsigned int desk_width(const SESSION *s)
{
    return (s->width + 3) & ~3U;
}

static unsigned int bytes_per_pixel(int depth)
{
    return depth == 8 ? 1 : depth == 24 ? 3 : 2;
}

static int lock_frame(SESSION *s, CORE_FRAME *f)
{
    memset(f, 0, sizeof(*f));
    f->cb = sizeof(*f);
    if (!s->core->FrameLock(s->core_ctx, f)) {
        session_end(s, 0, "the core has no screen");
        return 0;
    }
    if (f->bpp == 8 && f->palette == NULL) {
        s->core->FrameUnlock(s->core_ctx);
        session_end(s, 0, "the core's 8 bpp frame has no palette");
        return 0;
    }
    if (!rdp_same_mode(s, f)) {
        s->core->FrameUnlock(s->core_ctx);
        s->mode_due = 1;
        return 0;
    }
    return 1;
}

unsigned int update_row(unsigned char *d, const CORE_FRAME *f,
                        const unsigned char *table, unsigned int x,
                        unsigned int y, unsigned int n)
{
    unsigned int b = table != NULL ? 1 : f->bpp == 32 ? 3 : f->bpp / 8;
    unsigned int have = 0, i;
    const unsigned char *p;

    if (y < f->height && x < f->width) {
        have = f->width - x < n ? f->width - x : n;
        p = f->bits + (long) y * f->stride + (long) x * (f->bpp / 8);
        if (table != NULL) {
            reduce_pixels(d, table, p, have, f->bpp);
        } else if (f->bpp == 32) {
            for (i = 0; i < have; i++, p += 4) {
                d[3 * i] = p[0];
                d[3 * i + 1] = p[1];
                d[3 * i + 2] = p[2];
            }
        } else {
            memcpy(d, p, have * b);
            if (f->bpp == 16 && f->green_mask == 0x03E0) {
                for (i = 0; i < have; i++) {
                    d[2 * i + 1] &= 0x7F;
                }
            }
        }
    }
    memset(d + have * b, 0, (n - have) * b);
    return n * b;
}

static void send_palette(SESSION *s)
{
    unsigned char buf[MCS_SDIN_HEADER + SHARE_DATA_HEADER + 776];
    unsigned char pal[256][3];
    CORE_FRAME f;
    OUTS o;

    if (s->reduce != NULL) {
        reduce_palette(pal);
    } else {
        if (!lock_frame(s, &f)) {
            return;
        }
        memcpy(pal, f.palette, sizeof(pal));
        s->core->FrameUnlock(s->core_ctx);
    }
    share_begin(s, &o, buf, sizeof(buf));
    out_u16le(&o, UPDATE_PALETTE);
    out_u16le(&o, 0);
    out_u32le(&o, 256);
    out_bytes(&o, &pal[0][0], sizeof(pal));
    share_end(s, &o, T2_UPDATE);
}

static void make_tile(SESSION *s, unsigned char *raw, unsigned int x,
                      unsigned int y, unsigned int w, unsigned int h)
{
    UPDATE_TILE *t = &s->tile;
    unsigned int row = w * bytes_per_pixel(s->depth), size = row * h;
    unsigned int hdr = (s->caps.extra_flags & 0x0400) ? 0 : CD_HEADER;
    double start;
    int n = 0;
    OUTS o;

    if (s->caps.bitmap_compression) {
        start = log_ms();
        n = rle_encode(raw, (int) w, (int) h, s->depth, t->data + hdr,
                       (int) (size - hdr) - 1);
        s->stats.rle_ms += log_ms() - start;
    }
    t->x = x;
    t->y = y;
    t->w = w;
    t->h = h;
    if (n > 0 && hdr == 0) {
        t->flags = 0x0401;
        t->len = (unsigned int) n;
    } else if (n > 0) {
        t->flags = 0x0001;
        t->len = CD_HEADER + (unsigned int) n;
        out_init(&o, t->data, CD_HEADER);
        out_u16le(&o, 0);
        out_u16le(&o, (unsigned int) n);
        out_u16le(&o, row);
        out_u16le(&o, size);
    } else {
        t->flags = 0x0000;
        t->len = size;
        memcpy(t->data, raw, size);
    }
    t->ready = 1;
}

static int reads_unsent(const SESSION *s, const CORE_ORDER *o)
{
    const UPDATE_MAP *m = &s->map;
    long ch = (long) m->cell_h, cx, cy;
    RECT r;

    if (m->unsent == 0 || !order_reads(o, &r)) {
        return 0;
    }
    if (r.left < 0) {
        r.left = 0;
    }
    if (r.top < 0) {
        r.top = 0;
    }
    if (r.right > (long) desk_width(s)) {
        r.right = (long) desk_width(s);
    }
    if (r.bottom > (long) s->height) {
        r.bottom = (long) s->height;
    }
    for (cy = r.top / ch; cy * ch < r.bottom; cy++) {
        for (cx = r.left / UPDATE_CELL_W; cx * UPDATE_CELL_W < r.right;
             cx++) {
            if (!m->sent[cy * (long) m->cols + cx]) {
                return 1;
            }
        }
    }
    return 0;
}

static int map_box(const SESSION *s, const RECT *r, RECT *box)
{
    const UPDATE_MAP *m = &s->map;
    const UPDATE_CELL *c;
    long ch = (long) m->cell_h, cx, cy, ox, oy, x0, y0, x1, y1;
    long left = r->left, top = r->top, right = r->right, bottom = r->bottom;
    long bl = 0, bt = 0, br = 0, bb = 0;

    memset(box, 0, sizeof(*box));
    if (m->dirty == 0) {
        return 0;
    }
    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }
    if (right > (long) desk_width(s)) {
        right = (long) desk_width(s);
    }
    if (bottom > (long) s->height) {
        bottom = (long) s->height;
    }
    for (cy = top / ch; cy * ch < bottom; cy++) {
        oy = cy * ch;
        for (cx = left / UPDATE_CELL_W; cx * UPDATE_CELL_W < right; cx++) {
            ox = cx * UPDATE_CELL_W;
            c = &m->cell[cy * (long) m->cols + cx];
            if (c->x1 == 0 ||
                (long) (m->at[cy * (long) m->cols + cx] - s->map_now) > 0) {
                continue;
            }
            x0 = ox + c->x0 > left ? ox + c->x0 : left;
            y0 = oy + c->y0 > top ? oy + c->y0 : top;
            x1 = ox + c->x1 < right ? ox + c->x1 : right;
            y1 = oy + c->y1 < bottom ? oy + c->y1 : bottom;
            if (x0 >= x1 || y0 >= y1) {
                continue;
            }
            if (br == 0) {
                bl = x0;
                bt = y0;
                br = x1;
                bb = y1;
                continue;
            }
            if (x0 < bl) {
                bl = x0;
            }
            if (y0 < bt) {
                bt = y0;
            }
            if (x1 > br) {
                br = x1;
            }
            if (y1 > bb) {
                bb = y1;
            }
        }
    }
    if (br == 0) {
        return 0;
    }
    box->left = bl;
    box->top = bt;
    box->right = br;
    box->bottom = bb;
    return 1;
}

static void map_move(SESSION *s, const CORE_ORDER *o)
{
    long dx = (long) o->rect.left - o->src_x;
    long dy = (long) o->rect.top - o->src_y;
    RECT src, b;

    src.left = o->src_x;
    src.top = o->src_y;
    src.right = src.left + (o->rect.right - o->rect.left);
    src.bottom = src.top + (o->rect.bottom - o->rect.top);
    if (!map_box(s, &src, &b)) {
        return;
    }
    b.left += dx;
    b.top += dy;
    b.right += dx;
    b.bottom += dy;
    update_area(s, b.left > o->rect.left ? b.left : o->rect.left,
                b.top > o->rect.top ? b.top : o->rect.top,
                b.right < o->rect.right ? b.right : o->rect.right,
                b.bottom < o->rect.bottom ? b.bottom : o->rect.bottom);
}

static int map_covers(const SESSION *s, const RECT *r)
{
    const UPDATE_MAP *m = &s->map;
    const UPDATE_CELL *c;
    long ch = (long) m->cell_h, cx, cy, ox, oy;

    if (m->dirty == 0 || r->left < 0 || r->top < 0 ||
        r->right > (long) desk_width(s) || r->bottom > (long) s->height ||
        r->left >= r->right || r->top >= r->bottom) {
        return 0;
    }
    for (cy = r->top / ch; cy * ch < r->bottom; cy++) {
        oy = cy * ch;
        for (cx = r->left / UPDATE_CELL_W; cx * UPDATE_CELL_W < r->right;
             cx++) {
            ox = cx * UPDATE_CELL_W;
            c = &m->cell[cy * (long) m->cols + cx];
            if (c->x1 == 0 ||
                (r->left > ox ? r->left - ox : 0) < c->x0 ||
                (r->top > oy ? r->top - oy : 0) < c->y0 ||
                (r->right < ox + UPDATE_CELL_W ? r->right - ox :
                 UPDATE_CELL_W) > c->x1 ||
                (r->bottom < oy + ch ? r->bottom - oy : ch) > c->y1) {
                return 0;
            }
        }
    }
    return 1;
}

static int map_skip(SESSION *s, const RECT *r)
{
    UPDATE_MAP *m = &s->map;
    long ch = (long) m->cell_h, cx, cy, i;

    if (!map_covers(s, r)) {
        return 0;
    }
    for (cy = r->top / ch; cy * ch < r->bottom; cy++) {
        for (cx = r->left / UPDATE_CELL_W; cx * UPDATE_CELL_W < r->right;
             cx++) {
            i = cy * (long) m->cols + cx;
            if ((long) (m->at[i] - s->map_now) > 0) {
                m->at[i] = s->map_now;
            }
        }
    }
    return 1;
}

static void save_remove(SAVE_STATE *v, int k)
{
    memmove(&v->e[k], &v->e[k + 1],
            (size_t) (v->n - 1 - k) * sizeof(v->e[0]));
    v->n--;
}

static int save_order(SESSION *s, const CORE_ORDER *o, unsigned char *out,
                      unsigned int room)
{
    SAVE_STATE *v = &s->save;
    SAVE_ENTRY *e;
    long w = o->rect.right - o->rect.left, h = o->rect.bottom - o->rect.top;
    unsigned long pos, pels;
    unsigned int len;
    int k;

    for (k = v->n - 1; k >= 0 && v->e[k].num != o->save; k--) {
    }
    if (o->kind == CORE_ORDER_FREE) {
        if (k >= 0) {
            save_remove(v, k);
        }
        return -1;
    }
    if (o->kind == CORE_ORDER_RESTORE) {
        if (k < 0 || memcmp(&v->e[k].r, &o->rect, sizeof(RECT)) != 0) {
            if (k >= 0) {
                save_remove(v, k);
            }
            v->bitmaps++;
            s->stats.ord_bitmap++;
            update_area(s, o->rect.left, o->rect.top, o->rect.right,
                        o->rect.bottom);
            return -1;
        }
        e = &v->e[k];
        if (map_skip(s, &o->rect)) {
            s->stats.ord_covered++;
            save_remove(v, k);
            return -1;
        }
        len = order_savebitmap(&s->ord, e->pos, &e->r, 1, out, room);
        if (len == 0) {
            return 0;
        }
        if (e->stale.right > e->stale.left) {
            update_area(s, e->stale.left, e->stale.top, e->stale.right,
                        e->stale.bottom);
        }
        save_remove(v, k);
        v->restores++;
        return (int) len;
    }
    if (k >= 0) {
        save_remove(v, k);
    }
    if (v->size == 0 || !(s->ord_allow & ORD_A_SAVEBITMAP) || w <= 0 ||
        h <= 0 || o->rect.left < 0 || o->rect.top < 0 ||
        o->rect.right > (long) desk_width(s) ||
        o->rect.bottom > (long) s->height) {
        return -1;
    }
    pos = v->n > 0 ? v->e[v->n - 1].pos + v->e[v->n - 1].pels : 0;
    pels = ORD_SAVE_PELS(w, h);
    if (v->n >= SAVE_LEVELS || pels > v->size || pos > v->size - pels) {
        v->refused++;
        return -1;
    }
    len = order_savebitmap(&s->ord, pos, &o->rect, 0, out, room);
    if (len == 0) {
        return 0;
    }
    e = &v->e[v->n++];
    e->num = o->save;
    e->r = o->rect;
    e->pos = pos;
    e->pels = pels;
    map_box(s, &o->rect, &e->stale);
    v->saves++;
    return (int) len;
}

#define FRINGE_FILL_MAX 16

static void set_rect(RECT *r, long l, long t, long rr, long b)
{
    r->left = l;
    r->top = t;
    r->right = rr;
    r->bottom = b;
}

static int text_fringe(SESSION *s, const ORDER_SINK *k, const CORE_ORDER *o,
                       unsigned char *out, unsigned int room, RECT *box,
                       unsigned int *count)
{
    CORE_GLYPHREF r[CORE_TEXT_MAX];
    const CORE_GLYPH *g;
    CORE_ORDER fill;
    RECT band[4];
    const RECT *a = &o->rect;
    long l, t, rr, b;
    int i, nb = 0, len = 0, n, type;

    *count = 0;
    if (!(k->allow & ORD_A_OPAQUERECT) || o->nglyph < 1 ||
        o->nglyph > CORE_TEXT_MAX || a->left < o->op.left ||
        a->top < o->op.top || a->right > o->op.right ||
        a->bottom > o->op.bottom || !s->core->TextGet(s->core_ctx, o, r)) {
        return -1;
    }
    box->left = a->right;
    box->top = a->bottom;
    box->right = a->left;
    box->bottom = a->top;
    for (i = 0; i < o->nglyph; i++) {
        if (r[i].id >= CORE_GLYPH_PLACES) {
            return -1;
        }
        g = s->core->GlyphGet(s->core_ctx, r[i].id, r[i].serial);
        if (g == NULL) {
            return -1;
        }
        l = (long) r[i].x + g->ox;
        t = (long) o->y + g->oy;
        rr = l + g->cx;
        b = t + g->cy;
        box->left = l < box->left ? l : box->left;
        box->top = t < box->top ? t : box->top;
        box->right = rr > box->right ? rr : box->right;
        box->bottom = b > box->bottom ? b : box->bottom;
    }
    box->left = box->left > a->left ? box->left : a->left;
    box->top = box->top > a->top ? box->top : a->top;
    box->right = box->right < a->right ? box->right : a->right;
    box->bottom = box->bottom < a->bottom ? box->bottom : a->bottom;
    if (box->left >= box->right || box->top >= box->bottom) {
        band[nb++] = *a;
        set_rect(box, 0, 0, 0, 0);
    } else {
        if (a->top < box->top) {
            set_rect(&band[nb++], a->left, a->top, a->right, box->top);
        }
        if (box->bottom < a->bottom) {
            set_rect(&band[nb++], a->left, box->bottom, a->right, a->bottom);
        }
        if (a->left < box->left) {
            set_rect(&band[nb++], a->left, box->top, box->left, box->bottom);
        }
        if (box->right < a->right) {
            set_rect(&band[nb++], box->right, box->top, a->right, box->bottom);
        }
    }
    if (room < (unsigned int) nb * FRINGE_FILL_MAX) {
        return 0;
    }
    memset(&fill, 0, sizeof(fill));
    fill.kind = CORE_ORDER_FILL;
    fill.color = o->bg;
    fill.rop = 0xF0;
    for (i = 0; i < nb; i++) {
        fill.rect = band[i];
        n = order_core(&s->ord, k, &fill, out + len, room - (unsigned int) len,
                       &type);
        if (n <= 0) {
            return i == 0 ? -1 : len;
        }
        len += n;
        (*count)++;
        s->stats.ord_type[ord_slot(type)]++;
    }
    return len;
}

#define INVERT_MAX 16384

static unsigned int invert_xor(const CORE_ORDER *o)
{
    return o->rop == 0x5A ? (unsigned int) (o->color & 0xFF) : 0xFF;
}

static int same_xor(const CORE_ORDER *a, const CORE_ORDER *b)
{
    return b->kind == CORE_ORDER_FILL && b->rop == a->rop &&
           (a->rop == 0x55 || b->color == a->color);
}

static int bw_pairs(const CORE_FRAME *f, const RECT *r, unsigned int xor_c)
{
    unsigned char bw[256];
    const unsigned char *row, *c;
    unsigned int p, q;
    long x, y;

    for (p = 0; p < 256; p++) {
        c = f->palette + p * 3;
        bw[p] = (unsigned char) (c[0] == 0 && c[1] == 0 && c[2] == 0 ? 1 :
                c[0] == 255 && c[1] == 255 && c[2] == 255 ? 2 : 0);
    }
    for (y = r->top; y < r->bottom; y++) {
        row = f->bits + y * f->stride;
        for (x = r->left; x < r->right; x++) {
            q = row[x];
            p = q ^ xor_c;
            if (bw[p] == 0 || bw[q] != 3 - bw[p] || bw[255 - p] != bw[q] ||
                bw[255 - q] != bw[p]) {
                return 0;
            }
        }
    }
    return 1;
}

static int invert_bw(const CORE_FRAME *f, const CORE_ORDER *o)
{
    if (o->kind != CORE_ORDER_FILL || (o->rop != 0x55 && o->rop != 0x5A) ||
        f->bpp != 8 || f->palette == NULL ||
        (o->rect.right - o->rect.left) * (o->rect.bottom - o->rect.top) >
        INVERT_MAX) {
        return 0;
    }
    return bw_pairs(f, &o->rect, invert_xor(o));
}

static int reads_part(const CORE_ORDER *o, const RECT *r, RECT *a)
{
    RECT t;

    if (o->kind == CORE_ORDER_SAVE) {
        t = o->rect;
    } else if (!order_reads(o, &t)) {
        return 0;
    }
    a->left = t.left > r->left ? t.left : r->left;
    a->top = t.top > r->top ? t.top : r->top;
    a->right = t.right < r->right ? t.right : r->right;
    a->bottom = t.bottom < r->bottom ? t.bottom : r->bottom;
    return a->left < a->right && a->top < a->bottom;
}

static int read_later(SESSION *s, const CORE_FRAME *f, const CORE_ORDER *o,
                      int i, int n)
{
    unsigned long end = s->ord_next;
    long dx, dy;
    RECT a, m;
    int j, k;

    s->core->OrdersGet(s->core_ctx, &end, NULL, 0);
    if (end != s->ord_next) {
        return 1;
    }
    for (j = i + 1; j < n; j++) {
        if (!reads_part(&o[j], &o[i].rect, &a) || same_xor(&o[i], &o[j])) {
            continue;
        }
        if (o[j].kind != CORE_ORDER_COPY) {
            return 1;
        }
        dx = o[j].rect.left - o[j].src_x;
        dy = o[j].rect.top - o[j].src_y;
        m.left = a.left + dx;
        m.top = a.top + dy;
        m.right = a.right + dx;
        m.bottom = a.bottom + dy;
        for (k = j + 1; k < n; k++) {
            if (reads_part(&o[k], &m, &a) && !same_xor(&o[i], &o[k])) {
                return 1;
            }
        }
        if (!bw_pairs(f, &m, invert_xor(&o[i]))) {
            return 1;
        }
    }
    return 0;
}

static int drain(SESSION *s, const CORE_FRAME *f)
{
    CORE_ORDER o[32];
    ORDER_SINK k;
    ORDER_PDU *q = &s->ord_pdu;
    unsigned int count;
    int n, i, len, type, done;
    RECT box;

    rdp_take_damage(s);
    if (s->ord_skip) {
        s->ord_skip = 0;
        s->core->OrdersGet(s->core_ctx, &s->ord_next, NULL, 0);
    }
    s->map_now = s->ord_next;
    k.allow = s->ord_allow;
    k.depth = s->depth;
    k.frame_bpp = f->bpp;
    k.reduce = s->reduce;
    k.invert_bw = 0;
    k.brush_dy = s->caps.freerdp ? 7 : 0;
    for (;;) {
        n = s->core->OrdersGet(s->core_ctx, &s->ord_next, o, 32);
        if (n < 0) {
            s->stats.ord_lost++;
            s->save.n = 0;
            update_area(s, 0, 0, (long) s->width, (long) s->height);
            continue;
        }
        if (n == 0) {
            s->ord_due = 0;
            s->map_now = s->ord_next;
            return 1;
        }
        for (i = 0; i < n; i++) {
            s->map_now = s->ord_next - (unsigned long) (n - i);
            if (o[i].kind >= CORE_ORDER_SAVE &&
                o[i].kind <= CORE_ORDER_FREE) {
                len = save_order(s, &o[i], q->data + q->len,
                                 ORDER_PDU_BYTES - q->len);
                if (len == 0) {
                    s->ord_next -= (unsigned long) (n - i);
                    s->ord_due = 1;
                    return 0;
                }
                if (len > 0) {
                    q->len += (unsigned int) len;
                    q->count++;
                    s->stats.ord_type[ord_slot(ORD_SAVEBITMAP)]++;
                }
                continue;
            }
            if (map_skip(s, &o[i].rect)) {
                s->stats.ord_covered++;
                continue;
            }
            if (o[i].kind == CORE_ORDER_TEXT ||
                o[i].kind == CORE_ORDER_BITMAP) {
                len = -1;
                done = 0;
                count = 0;
                if (s->ord_allow == 0) {
                } else if (o[i].kind == CORE_ORDER_TEXT) {
                    len = glyphs_text(s, &k, &o[i],
                                      s->ord_next - (unsigned long) (n - i),
                                      q->data + q->len,
                                      ORDER_PDU_BYTES - q->len, &count,
                                      &done, &type);
                } else if (!reads_unsent(s, &o[i])) {
                    len = bitmaps_order(s, &k, f, &o[i], q->data + q->len,
                                        ORDER_PDU_BYTES - q->len, &count,
                                        &done);
                    type = ORD_MEMBLT;
                }
                if (len < 0 || (!done && len == 0 && q->len == 0)) {
                    if (len == 0 && o[i].kind == CORE_ORDER_TEXT) {
                        s->glyph.why[GLYPHS_W_ROOM]++;
                    }
                    len = -1;
                    if (o[i].kind == CORE_ORDER_TEXT && s->ord_allow != 0) {
                        len = text_fringe(s, &k, &o[i], q->data + q->len,
                                          ORDER_PDU_BYTES - q->len, &box,
                                          &count);
                    }
                    if (len == 0 && q->len > 0) {
                        s->ord_next -= (unsigned long) (n - i);
                        s->ord_due = 1;
                        return 0;
                    }
                    if (len > 0) {
                        q->len += (unsigned int) len;
                        q->count += count;
                        s->stats.ord_fringe++;
                        if (box.left < box.right) {
                            update_area(s, box.left, box.top, box.right,
                                        box.bottom);
                        }
                        continue;
                    }
                    s->stats.ord_bitmap++;
                    update_area(s, o[i].rect.left, o[i].rect.top,
                                o[i].rect.right, o[i].rect.bottom);
                    continue;
                }
                q->len += (unsigned int) len;
                q->count += count;
                if (!done) {
                    s->ord_next -= (unsigned long) (n - i);
                    s->ord_due = 1;
                    return 0;
                }
                s->stats.ord_type[ord_slot(type)]++;
                continue;
            }
            len = -1;
            if (s->ord_allow != 0 && !reads_unsent(s, &o[i])) {
                k.invert_bw = s->depth == 8 && invert_bw(f, &o[i]) &&
                              !read_later(s, f, o, i, n);
                len = order_core(&s->ord, &k, &o[i], q->data + q->len,
                                 ORDER_PDU_BYTES - q->len, &type);
                k.invert_bw = 0;
            }
            if (len == 0) {
                s->ord_next -= (unsigned long) (n - i);
                s->ord_due = 1;
                return 0;
            }
            if (len < 0) {
                s->stats.ord_bitmap++;
                update_area(s, o[i].rect.left, o[i].rect.top,
                            o[i].rect.right, o[i].rect.bottom);
                continue;
            }
            if (o[i].kind == CORE_ORDER_COPY) {
                map_move(s, &o[i]);
            }
            q->len += (unsigned int) len;
            q->count++;
            s->stats.ord_type[ord_slot(type)]++;
        }
    }
}

static int next_tile(SESSION *s)
{
    unsigned char raw[RAW_MAX];
    UPDATE_MAP *m = &s->map;
    unsigned int total = m->cols * m->rows, i, k = 0;
    unsigned int x, y, w, h, row, r;
    UPDATE_CELL *c = NULL;
    CORE_FRAME f;

    if (m->dirty == 0) {
        return 0;
    }
    if (!lock_frame(s, &f)) {
        return 0;
    }
    if (!drain(s, &f) || s->ord_pdu.count > 0) {
        s->core->FrameUnlock(s->core_ctx);
        return 0;
    }
    for (i = 0; i < total; i++) {
        k = (m->next + i) % total;
        if (m->cell[k].x1 != 0) {
            c = &m->cell[k];
            break;
        }
    }
    if (c == NULL) {
        s->core->FrameUnlock(s->core_ctx);
        m->dirty = 0;
        return 0;
    }
    x = k % m->cols * UPDATE_CELL_W + c->x0;
    y = k / m->cols * m->cell_h + c->y0;
    w = c->x1 - c->x0;
    h = c->y1 - c->y0;
    c->x1 = 0;
    m->dirty--;
    m->next = m->dirty == 0 ? 0 : (k + 1) % total;
    if (!m->sent[k]) {
        m->sent[k] = 1;
        m->unsent--;
    }
    row = w * bytes_per_pixel(s->depth);
    for (r = 0; r < h; r++) {
        update_row(raw + r * row, &f, s->reduce, x, y + h - 1 - r, w);
    }
    s->core->FrameUnlock(s->core_ctx);
    make_tile(s, raw, x, y, w, h);
    return 1;
}

static void drain_now(SESSION *s)
{
    CORE_FRAME f;

    if (lock_frame(s, &f)) {
        drain(s, &f);
        s->core->FrameUnlock(s->core_ctx);
    }
}

static void send_orders(SESSION *s)
{
    unsigned char buf[MCS_SDIN_HEADER + SHARE_DATA_HEADER + 8 +
                      ORDER_PDU_BYTES];
    ORDER_PDU *q = &s->ord_pdu;
    OUTS o;

    share_begin(s, &o, buf, sizeof(buf));
    out_u16le(&o, 0);
    out_u16le(&o, 0);
    out_u16le(&o, q->count);
    out_u16le(&o, 0);
    out_bytes(&o, q->data, q->len);
    share_end(s, &o, T2_UPDATE);
    s->stats.ord_sent += q->count;
    s->stats.ord_bytes += q->len;
    s->stats.ord_pdus++;
    q->count = 0;
    q->len = 0;
}

static void send_tiles(SESSION *s)
{
    unsigned char buf[MCS_SDIN_HEADER + SHARE_DATA_HEADER + 4 +
                      UPDATE_TILE_BYTES];
    UPDATE_TILE *t = &s->tile;
    unsigned int count, n = 0, used = 0;
    OUTS o;

    share_begin(s, &o, buf, sizeof(buf));
    out_u16le(&o, UPDATE_BITMAP);
    count = out_reserve(&o, 2);
    while (t->ready || next_tile(s)) {
        if (used + UPDATE_TILE_HEADER + t->len > UPDATE_TILE_BYTES) {
            break;
        }
        out_u16le(&o, t->x);
        out_u16le(&o, t->y);
        out_u16le(&o, t->x + t->w - 1);
        out_u16le(&o, t->y + t->h - 1);
        out_u16le(&o, t->w);
        out_u16le(&o, t->h);
        out_u16le(&o, (unsigned int) s->depth);
        out_u16le(&o, t->flags);
        out_u16le(&o, t->len);
        out_bytes(&o, t->data, t->len);
        used += UPDATE_TILE_HEADER + t->len;
        n++;
        if (t->flags != 0) {
            s->stats.tiles_rle++;
        } else {
            s->stats.tiles_raw++;
        }
        s->stats.pixel_bytes += t->w * t->h * bytes_per_pixel(s->depth);
        s->stats.data_bytes += t->len;
        t->ready = 0;
    }
    if (n == 0) {
        return;
    }
    out_put_u16le(&o, count, n);
    share_end(s, &o, T2_UPDATE);
}

static void clear_map(SESSION *s)
{
    UPDATE_MAP *m = &s->map;

    memset(m->cell, 0, m->cols * m->rows * sizeof(m->cell[0]));
    memset(m->sent, 0, m->cols * m->rows);
    m->unsent = m->cols * m->rows;
    m->dirty = 0;
    m->next = 0;
    s->tile.ready = 0;
}

static void log_stats(SESSION *s, const char *what)
{
    const UPDATE_STATS *st = &s->stats;
    unsigned long tenths = (unsigned long) (st->rle_ms * 10.0 + 0.5);
    unsigned long percent = 0;

    if (st->pixel_bytes > 0) {
        percent = (unsigned long) (100.0 * st->data_bytes /
                                   st->pixel_bytes + 0.5);
    }
    log_msg(LOG_INFO, "session %s: %s: %lu tiles (%lu RLE, %lu raw); "
            "%lu bytes of pixels went out as %lu bytes (%lu%%); RLE "
            "encoding %lu.%lu ms", s->peer, what,
            st->tiles_rle + st->tiles_raw, st->tiles_rle, st->tiles_raw,
            st->pixel_bytes, st->data_bytes, percent, tenths / 10,
            tenths % 10);
}

static void log_orders(SESSION *s, const char *what)
{
    const UPDATE_STATS *st = &s->stats;
    char kinds[160];
    int i, n = 0;

    if (st->ord_sent + st->ord_bitmap + st->ord_covered + st->ord_lost +
        st->ord_fringe == 0) {
        return;
    }
    kinds[0] = '\0';
    for (i = 0; i < ORD_TYPES; i++) {
        if (st->ord_type[i] != 0) {
            n += sprintf(kinds + n, "%s%s %lu", n > 0 ? ", " : "",
                         ord_names[i], st->ord_type[i]);
        }
    }
    log_msg(LOG_INFO, "session %s: %s: %lu drawing orders in %lu updates, "
            "%lu bytes (%s); %lu went as bitmaps, %lu text orders went as "
            "fills and their glyphs as bitmaps, %lu were left out (their "
            "area went as bitmaps anyway), the core's log ran over %lu "
            "times", s->peer, what, st->ord_sent, st->ord_pdus,
            st->ord_bytes, n > 0 ? kinds : "none", st->ord_bitmap,
            st->ord_fringe, st->ord_covered, st->ord_lost);
    if (st->ord_fringe + st->ord_bitmap > 0 && s->glyph.usable) {
        log_msg(LOG_INFO, "session %s: %s: text orders that could not go "
                "as text orders: setup %lu, run lost %lu, glyph lost %lu, "
                "origin %lu, no cache %lu, coding %lu, room %lu (task M6.4)",
                s->peer, what, s->glyph.why[GLYPHS_W_SETUP],
                s->glyph.why[GLYPHS_W_RUN], s->glyph.why[GLYPHS_W_GLYPH],
                s->glyph.why[GLYPHS_W_RANGE], s->glyph.why[GLYPHS_W_CACHE],
                s->glyph.why[GLYPHS_W_CODE], s->glyph.why[GLYPHS_W_ROOM]);
    }
    if (s->glyph.fills > 0) {
        log_msg(LOG_INFO, "session %s: %s: %lu Cache Glyph orders (in the "
                "drawing orders above) with %lu glyphs, %lu bytes", s->peer,
                what, s->glyph.fills, s->glyph.glyphs, s->glyph.fill_bytes);
    }
    if (s->save.saves + s->save.bitmaps + s->save.refused > 0) {
        log_msg(LOG_INFO, "session %s: %s: menu saves: %lu SaveBitmap "
                "saves and %lu restores (in the drawing orders above); %lu "
                "restores went as bitmaps, %lu saves found no room",
                s->peer, what, s->save.saves, s->save.restores,
                s->save.bitmaps, s->save.refused);
    }
    if (s->bmp.hits + s->bmp.fills + s->bmp.gone + s->bmp.nofit +
        s->bmp.notake > 0) {
        log_msg(LOG_INFO, "session %s: %s: bitmap caches: %lu Cache Bitmap "
                "orders (%lu compressed, %lu bytes) and %lu color tables "
                "(in the drawing orders above), %lu MemBlts of a bitmap "
                "the client had; as bitmaps: %lu bitmap orders whose "
                "bitmap was gone, %lu that fit no cache or no update, %lu "
                "that this client cannot take (no MemBlt, or a ROP it "
                "cannot draw)", s->peer, what, s->bmp.fills,
                s->bmp.fill_rle, s->bmp.fill_bytes, s->bmp.tables,
                s->bmp.hits, s->bmp.gone, s->bmp.nofit, s->bmp.notake);
    }
}

static int make_table(SESSION *s)
{
    CORE_FRAME f;
    double start;

    if (s->reduce == NULL) {
        s->reduce = (unsigned char *) malloc(REDUCE_TABLE_SIZE);
        if (s->reduce == NULL) {
            session_end(s, 0, "no memory for the 8 bpp lookup table");
            return 0;
        }
    }
    if (!lock_frame(s, &f)) {
        return 0;
    }
    s->core->FrameUnlock(s->core_ctx);
    start = log_ms();
    reduce_table(s->reduce, &f);
    start = log_ms() - start;
    if (start < 0.0) {
        start = 0.0;
    }
    log_msg(LOG_INFO, "session %s: the %d bpp console goes out as 8 bpp "
            "with the fixed palette; lookup table made in %lu ms", s->peer,
            s->console_depth, (unsigned long) (start + 0.5));
    return 1;
}

void update_screen(SESSION *s)
{
    UPDATE_MAP *m = &s->map;
    unsigned int cell_h;
    char text[96], gtext[200], btext[200];

    cell_h = RAW_MAX / (UPDATE_CELL_W * bytes_per_pixel(s->depth));
    if (cell_h > CELL_H_MAX) {
        cell_h = CELL_H_MAX;
    }
    m->cols = (desk_width(s) + UPDATE_CELL_W - 1) / UPDATE_CELL_W;
    m->rows = (s->height + cell_h - 1) / cell_h;
    m->cell_h = cell_h;
    if ((unsigned long) m->cols * m->rows > UPDATE_CELLS_MAX) {
        session_end(s, 0, "the desktop (%u x %u) is too big for the dirty "
                    "map", desk_width(s), s->height);
        m->cols = 0;
        m->rows = 0;
        return;
    }
    clear_map(s);
    if (s->depth == 8 && s->console_depth > 8) {
        if (!make_table(s)) {
            return;
        }
    } else if (s->reduce != NULL) {
        free(s->reduce);
        s->reduce = NULL;
    }
    if (s->depth == 8) {
        s->upd_palette = 1;
    }
    update_area(s, 0, 0, (long) s->width, (long) s->height);

    s->ord_allow = 0;
    if (CAPS_HAVE(&s->caps, CAPS_ORDER)) {
        s->ord_allow = order_allowed(s->caps.order_flags,
                                     s->caps.order_support, s->depth);
    }
    s->ord_skip = 1;
    s->ord_due = 1;
    order_allowed_text(s->ord_allow, text);
    log_msg(LOG_INFO, "session %s: drawing orders this client takes: %s "
            "(orderFlags 0x%04x)", s->peer, text, s->caps.order_flags);
    glyphs_caps(s, gtext);
    if (!s->glyph.usable) {
        s->ord_allow &= ~(unsigned int) (ORD_A_FASTINDEX | ORD_A_GLYPHINDEX);
    }
    log_msg(LOG_INFO, "session %s: %s; text orders: %s", s->peer, gtext,
            (s->ord_allow & ORD_A_FASTINDEX) ? "FastIndex" :
            (s->ord_allow & ORD_A_GLYPHINDEX) ? "GlyphIndex" : "none");
    s->save.n = 0;
    s->save.size = 0;
    if (s->ord_allow & ORD_A_SAVEBITMAP) {
        s->save.size = s->caps.desktop_save_size < ORD_SAVE_MAX ?
                       s->caps.desktop_save_size : ORD_SAVE_MAX;
        if (s->save.size < ORD_SAVE_PELS(1, 1)) {
            s->save.size = 0;
            s->ord_allow &= ~(unsigned int) ORD_A_SAVEBITMAP;
        }
    }
    log_msg(LOG_INFO, "session %s: menu saves: %s (the client's save "
            "buffer: %lu pixels)", s->peer,
            s->save.size > 0 ? "SaveBitmap" : "as bitmaps",
            s->caps.desktop_save_size);
    bitmaps_reset(s);
    bitmaps_caps(s, btext);
    if (!s->bmp.usable) {
        s->ord_allow &= ~(unsigned int) ORD_A_MEMBLT;
    }
    log_msg(LOG_INFO, "session %s: %s; bitmap orders: %s", s->peer, btext,
            (s->ord_allow & ORD_A_MEMBLT) ? "MemBlt" : "none");
}

static int mark(SESSION *s, long left, long top, long right, long bottom,
                unsigned long at)
{
    UPDATE_MAP *m = &s->map;
    long w = (long) desk_width(s), h = (long) s->height;
    long ch = (long) m->cell_h;
    long cx, cy, ox, oy, x0, y0, x1, y1, i;
    UPDATE_CELL *c;

    if (m->cols == 0) {
        return 0;
    }
    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }
    if (right > w) {
        right = w;
    }
    if (bottom > h) {
        bottom = h;
    }
    if (left >= right || top >= bottom) {
        return 0;
    }
    left &= ~3L;
    right = (right + 3) & ~3L;
    for (cy = top / ch; cy * ch < bottom; cy++) {
        oy = cy * ch;
        y0 = top > oy ? top - oy : 0;
        y1 = bottom < oy + ch ? bottom - oy : ch;
        for (cx = left / UPDATE_CELL_W; cx * UPDATE_CELL_W < right; cx++) {
            ox = cx * UPDATE_CELL_W;
            x0 = left > ox ? left - ox : 0;
            x1 = right < ox + UPDATE_CELL_W ? right - ox : UPDATE_CELL_W;
            i = cy * (long) m->cols + cx;
            c = &m->cell[i];
            if (c->x1 == 0) {
                c->x0 = (unsigned char) x0;
                c->y0 = (unsigned char) y0;
                c->x1 = (unsigned char) x1;
                c->y1 = (unsigned char) y1;
                m->at[i] = at;
                m->dirty++;
                continue;
            }
            if ((long) (at - m->at[i]) < 0) {
                m->at[i] = at;
            }
            if (x0 < c->x0) {
                c->x0 = (unsigned char) x0;
            }
            if (y0 < c->y0) {
                c->y0 = (unsigned char) y0;
            }
            if (x1 > c->x1) {
                c->x1 = (unsigned char) x1;
            }
            if (y1 > c->y1) {
                c->y1 = (unsigned char) y1;
            }
        }
    }
    return 1;
}

int update_area(SESSION *s, long left, long top, long right, long bottom)
{
    return mark(s, left, top, right, bottom, s->map_now);
}

int update_damage(SESSION *s, const RECT *r, unsigned long at)
{
    return mark(s, r->left, r->top, r->right, r->bottom, at);
}

void update_stop(SESSION *s)
{
    clear_map(s);
    s->map.cols = 0;
    s->map.rows = 0;
    s->upd_palette = 0;
    s->ord_due = 0;
}

void update_pump(SESSION *s)
{
    while (s->state == ST_ACTIVE && s->ending == END_NO && !s->suppressed &&
           !s->mode_due && SESSION_QUEUED(s) < UPDATE_QUEUE_MAX) {
        if (s->upd_palette) {
            s->upd_palette = 0;
            send_palette(s);
        } else if (s->tile.ready) {
            send_tiles(s);
        } else if (s->ord_pdu.count > 0) {
            send_orders(s);
        } else if (s->ord_due) {
            drain_now(s);
        } else if (s->map.dirty > 0) {
            send_tiles(s);
        } else {
            break;
        }
    }
    if (!s->stats.first_logged && s->map.dirty == 0 && !s->tile.ready &&
        s->stats.tiles_rle + s->stats.tiles_raw > 0) {
        s->stats.first_logged = 1;
        log_stats(s, "first screen");
    }
}

void update_done(SESSION *s)
{
    if (s->stats.tiles_rle + s->stats.tiles_raw > 0) {
        log_stats(s, "screen output");
    }
    log_orders(s, "screen output");
    free(s->reduce);
    s->reduce = NULL;
}
