/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <stddef.h>
#include <stdarg.h>
#include <windef.h>

#include "record.h"

static ULONG rec_area(LONG l, LONG t, LONG r, LONG b)
{
    return (ULONG) (r - l) * (ULONG) (b - t);
}

static void rec_union(MR_RECT *p, LONG l, LONG t, LONG r, LONG b)
{
    if (l < p->left) {
        p->left = l;
    }
    if (t < p->top) {
        p->top = t;
    }
    if (r > p->right) {
        p->right = r;
    }
    if (b > p->bottom) {
        p->bottom = b;
    }
}

static ULONG rec_union_area(const MR_RECT *p, LONG l, LONG t, LONG r, LONG b)
{
    if (p->left < l) {
        l = p->left;
    }
    if (p->top < t) {
        t = p->top;
    }
    if (p->right > r) {
        r = p->right;
    }
    if (p->bottom > b) {
        b = p->bottom;
    }
    return rec_area(l, t, r, b);
}

void rec_init(REC_LIST *pl, LONG cx, LONG cy)
{
    pl->cx = cx > 0 ? cx : 0;
    pl->cy = cy > 0 ? cy : 0;
    pl->flags = 0;
    pl->n = 0;
    pl->calls = 0;
}

int rec_add(REC_LIST *pl, LONG left, LONG top, LONG right, LONG bottom)
{
    int was_idle;
    ULONG i, best, grow, best_grow, a;
    MR_RECT *p;

    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }
    if (right > pl->cx) {
        right = pl->cx;
    }
    if (bottom > pl->cy) {
        bottom = pl->cy;
    }
    if (left >= right || top >= bottom) {
        return 0;
    }

    pl->calls++;
    was_idle = (pl->n == 0 && pl->flags == 0);
    if (pl->flags & MR_F_ALL) {
        return 0;
    }

    for (i = 0; i < pl->n; i++) {
        p = &pl->r[i];
        if (left >= p->left && top >= p->top &&
            right <= p->right && bottom <= p->bottom) {
            return 0;
        }
    }

    a = rec_area(left, top, right, bottom);
    if (pl->n > 0) {
        p = &pl->r[pl->n - 1];
        if (rec_union_area(p, left, top, right, bottom) <=
            rec_area(p->left, p->top, p->right, p->bottom) + a) {
            rec_union(p, left, top, right, bottom);
            return was_idle;
        }
    }

    if (pl->n < MR_RECTS) {
        p = &pl->r[pl->n];
        p->left = left;
        p->top = top;
        p->right = right;
        p->bottom = bottom;
        pl->n++;
        return was_idle;
    }

    best = 0;
    best_grow = 0xFFFFFFFFUL;
    for (i = 0; i < MR_RECTS; i++) {
        p = &pl->r[i];
        grow = rec_union_area(p, left, top, right, bottom) -
               rec_area(p->left, p->top, p->right, p->bottom);
        if (grow < best_grow) {
            best_grow = grow;
            best = i;
        }
    }
    rec_union(&pl->r[best], left, top, right, bottom);
    return was_idle;
}

int rec_flag(REC_LIST *pl, ULONG flags)
{
    int was_idle = (pl->n == 0 && pl->flags == 0);

    flags &= MR_F_ALL | MR_F_PALETTE | MR_F_MODE;
    if (flags == 0) {
        return 0;
    }
    pl->flags |= flags;
    if (flags & MR_F_ALL) {
        pl->n = 0;
    }
    return was_idle;
}

void rec_take(REC_LIST *pl, MR_OUT *po)
{
    ULONG i;

    po->flags |= pl->flags;
    po->calls = pl->calls;
    po->nrects = 0;
    if (!(pl->flags & MR_F_ALL)) {
        for (i = 0; i < pl->n && i < MR_RECTS; i++) {
            po->rect[i] = pl->r[i];
        }
        po->nrects = i;
    }
    pl->n = 0;
    pl->flags = 0;
}

ULONG rec_flush(REC_LIST *pl, ULONG bpp, ULONG room, MR_RECT *out,
                ULONG *flags)
{
    MR_RECT todo[MR_RECTS];
    MR_RECT *p;
    ULONG n = 0, i, k = 0, rowb, rows, fit;

    *flags = pl->flags;
    if (pl->flags & MR_F_ALL) {
        if (pl->cx > 0 && pl->cy > 0) {
            todo[0].left = 0;
            todo[0].top = 0;
            todo[0].right = pl->cx;
            todo[0].bottom = pl->cy;
            n = 1;
        }
    } else {
        for (n = 0; n < pl->n && n < MR_RECTS; n++) {
            todo[n] = pl->r[n];
        }
    }
    pl->flags = 0;
    pl->n = 0;

    for (i = 0; i < n; i++) {
        p = &todo[i];
        if (bpp == 4) {
            p->left &= ~1L;
            p->right = (p->right + 1) & ~1L;
        }
        rowb = MR_ROW_BYTES(p->left, p->right, bpp);
        rows = (ULONG) (p->bottom - p->top);
        fit = rowb != 0 ? room / rowb : rows;
        if (k == MR_RECTS || fit == 0) {
            break;
        }
        if (fit > rows) {
            fit = rows;
        }
        out[k] = *p;
        out[k].bottom = p->top + (LONG) fit;
        k++;
        room -= fit * rowb;
        if (fit < rows) {
            p->top += (LONG) fit;
            break;
        }
    }
    for (; i < n; i++) {
        pl->r[pl->n++] = todo[i];
    }
    if (pl->n != 0) {
        *flags |= MR_F_MORE;
    }
    return k;
}

int rec_move(REC_LIST *pl, LONG l, LONG t, LONG r, LONG b, LONG dx,
             LONG dy)
{
    ULONG i, n = pl->n;
    LONG ml, mt, mr, mb;
    int wake = 0;

    if (l >= r || t >= b || (pl->flags & MR_F_ALL)) {
        return 0;
    }
    for (i = 0; i < n && i < MR_RECTS; i++) {
        ml = pl->r[i].left + dx;
        mt = pl->r[i].top + dy;
        mr = pl->r[i].right + dx;
        mb = pl->r[i].bottom + dy;
        if (ml < l) {
            ml = l;
        }
        if (mt < t) {
            mt = t;
        }
        if (mr > r) {
            mr = r;
        }
        if (mb > b) {
            mb = b;
        }
        if (ml < mr && mt < mb && rec_add(pl, ml, mt, mr, mb)) {
            wake = 1;
        }
    }
    return wake;
}

int rec_bounds(const REC_LIST *pl, LONG l, LONG t, LONG r, LONG b,
               MR_RECT *out)
{
    ULONG i;
    LONG ml, mt, mr, mb;
    int have = 0;

    out->left = out->top = out->right = out->bottom = 0;
    if (l >= r || t >= b) {
        return 0;
    }
    if (pl->flags & MR_F_ALL) {
        out->left = l;
        out->top = t;
        out->right = r;
        out->bottom = b;
        return 1;
    }
    for (i = 0; i < pl->n && i < MR_RECTS; i++) {
        ml = pl->r[i].left > l ? pl->r[i].left : l;
        mt = pl->r[i].top > t ? pl->r[i].top : t;
        mr = pl->r[i].right < r ? pl->r[i].right : r;
        mb = pl->r[i].bottom < b ? pl->r[i].bottom : b;
        if (ml >= mr || mt >= mb) {
            continue;
        }
        if (!have) {
            out->left = ml;
            out->top = mt;
            out->right = mr;
            out->bottom = mb;
            have = 1;
            continue;
        }
        if (ml < out->left) {
            out->left = ml;
        }
        if (mt < out->top) {
            out->top = mt;
        }
        if (mr > out->right) {
            out->right = mr;
        }
        if (mb > out->bottom) {
            out->bottom = mb;
        }
    }
    return have;
}
