/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <stddef.h>
#include <stdarg.h>
#ifdef MR_LAYER
#include <windows.h>
#else
#include <windef.h>
#include <wingdi.h>
#endif
#include <winddi.h>

#include "shadow.h"

#ifndef FO_GRAY16
#define FO_GRAY16 0
#endif

MR_PDEV         *g_pdev[MR_MAXPDEV];
MR_PDEV         *g_cur;
ULONG            g_engine;
ULONG            g_reads;
MR_PTR           g_ptr;
static MR_PTROUT        g_shape;
ULONG            g_skipped;
ULONG            g_saves;
ULONG            g_savefail;
static ULONG            g_orders;
static ULONG            g_ordcalls;
static ULONG            g_text;
static ULONG            g_textcalls;
static ULONG            g_bitmaps;
static ULONG            g_bmpcalls;
static ULONG            g_patterns;
static ULONG            g_patcalls;
static ULONG            g_textmiss;
static ULONG            g_textwhy[8];
static ULONG            g_rectwhy[MR_RW_N];
static ULONG            g_rectpix[MR_RW_N];
static ULONG            g_ordmiss;
static ULONG            g_savenum;
static ULONG            g_saveords;
static ULONG            g_restords;

void mr_zero(PVOID pv, ULONG cb)
{
    BYTE *pb = (BYTE *) pv;

    while (cb != 0) {
        *pb++ = 0;
        cb--;
    }
}

MR_PDEV *mr_find(PVOID pv)
{
    ULONG i;

    if (pv != NULL) {
        for (i = 0; i < MR_MAXPDEV; i++) {
            if (g_pdev[i] == (MR_PDEV *) pv) {
                return g_pdev[i];
            }
        }
    }
    return NULL;
}

void mr_rect(MR_PDEV *ppdev, CLIPOBJ *pco,
                    LONG l, LONG t, LONG r, LONG b, ULONG why)
{
    LONG x, w, h;

    if (ppdev == NULL || ppdev->fullscreen) {
        return;
    }
    if (l > r) {
        x = l;
        l = r;
        r = x;
    }
    if (t > b) {
        x = t;
        t = b;
        b = x;
    }
    if (pco != NULL && pco->iDComplexity != DC_TRIVIAL) {
        if (l < pco->rclBounds.left) {
            l = pco->rclBounds.left;
        }
        if (t < pco->rclBounds.top) {
            t = pco->rclBounds.top;
        }
        if (r > pco->rclBounds.right) {
            r = pco->rclBounds.right;
        }
        if (b > pco->rclBounds.bottom) {
            b = pco->rclBounds.bottom;
        }
    }
    w = (r < ppdev->cx ? r : ppdev->cx) - (l > 0 ? l : 0);
    h = (b < ppdev->cy ? b : ppdev->cy) - (t > 0 ? t : 0);
    if (why < MR_RW_N) {
        g_rectwhy[why]++;
        if (w > 0 && h > 0) {
            g_rectpix[why] += (ULONG) w * (ULONG) h;
        }
    }
    if (rec_add(&ppdev->rec, l, t, r, b)) {
        mr_signal();
    }
}

void mr_rcl(MR_PDEV *ppdev, CLIPOBJ *pco, RECTL *prcl, ULONG why)
{
    if (prcl != NULL) {
        mr_rect(ppdev, pco, prcl->left, prcl->top, prcl->right,
                prcl->bottom, why);
    }
}

void mr_all(MR_PDEV *ppdev, CLIPOBJ *pco, ULONG why)
{
    if (ppdev != NULL) {
        mr_rect(ppdev, pco, 0, 0, ppdev->cx, ppdev->cy, why);
    }
}

static void mr_path(MR_PDEV *ppdev, CLIPOBJ *pco, PATHOBJ *ppo, LONG extra)
{
    RECTFX rfx;

    if (ppdev == NULL) {
        return;
    }
    if (ppo == NULL) {
        mr_all(ppdev, pco, MR_RW_SHAPE);
        return;
    }
    PATHOBJ_vGetBounds(ppo, &rfx);
    mr_rect(ppdev, pco,
            (rfx.xLeft >> 4) - extra,
            (rfx.yTop >> 4) - extra,
            ((rfx.xRight + 15) >> 4) + 1 + extra,
            ((rfx.yBottom + 15) >> 4) + 1 + extra, MR_RW_SHAPE);
}

static void mr_stroke(MR_PDEV *ppdev, CLIPOBJ *pco, PATHOBJ *ppo,
                      LINEATTRS *pla)
{
    if (pla != NULL && (pla->fl & LA_GEOMETRIC)) {
        mr_all(ppdev, pco, MR_RW_SHAPE);
    } else {
        mr_path(ppdev, pco, ppo, 1);
    }
}

static void mr_count_read(SURFOBJ *psoSrc, MR_PDEV *ppdevDst)
{
    if (ppdevDst == NULL && mr_target(psoSrc) != NULL) {
        g_reads++;
    }
}

void mr_copy(PVOID dst, const VOID *src, ULONG cb)
{
    __asm {
        mov     edi, dst
        mov     esi, src
        mov     ecx, cb
        mov     edx, ecx
        shr     ecx, 2
        rep     movsd
        mov     ecx, edx
        and     ecx, 3
        rep     movsb
    }
}

static void mr_surf_make(MR_PDEV *ppdev, LONG cx, PVOID *ppBits,
                         HSURF *phsurf, SURFOBJ **ppso)
{
    ULONG    stride = (ULONG) cx * ppdev->bpp / 8;
    SIZEL    sizl;
    PVOID    bits;
    HBITMAP  hbm;
    SURFOBJ *pso = NULL;

    bits = mr_alloc(stride * (ULONG) cx);
    if (bits == NULL) {
        return;
    }
    sizl.cx = cx;
    sizl.cy = cx;
    hbm = EngCreateBitmap(sizl, (LONG) stride, ppdev->iFormat, BMF_TOPDOWN,
                          bits);
    if (hbm != NULL) {
        pso = EngLockSurface((HSURF) hbm);
    }
    if (pso == NULL) {
        if (hbm != NULL) {
            EngDeleteSurface((HSURF) hbm);
        }
        mr_free(bits);
        return;
    }
    *ppBits = bits;
    *phsurf = (HSURF) hbm;
    *ppso = pso;
}

static void mr_surf_free(PVOID *ppBits, HSURF *phsurf, SURFOBJ **ppso)
{
    if (*ppso != NULL) {
        EngUnlockSurface(*ppso);
        EngDeleteSurface(*phsurf);
        mr_free(*ppBits);
        *ppso = NULL;
        *phsurf = NULL;
        *ppBits = NULL;
    }
}

void mr_shadow_make(MR_PDEV *ppdev)
{
    ULONG    stride = ((ULONG) ppdev->cx * ppdev->bpp + 31) / 32 * 4;
    SIZEL    sizl;
    PVOID    bits;
    HBITMAP  hbm;
    SURFOBJ *pso = NULL;

    if ((ULONG) ppdev->cy > MR_SHADOW_MAX / stride) {
        return;
    }
    bits = mr_alloc(stride * (ULONG) ppdev->cy);
    if (bits == NULL) {
        return;
    }
    sizl.cx = ppdev->cx;
    sizl.cy = ppdev->cy;
    hbm = EngCreateBitmap(sizl, (LONG) stride, ppdev->iFormat, BMF_TOPDOWN,
                          bits);
    if (hbm != NULL) {
        pso = EngLockSurface((HSURF) hbm);
    }
    if (pso == NULL) {
        if (hbm != NULL) {
            EngDeleteSurface((HSURF) hbm);
        }
        mr_free(bits);
        return;
    }
    ppdev->shadowBits = bits;
    ppdev->hsurfShadow = (HSURF) hbm;
    ppdev->psoShadow = pso;
    ppdev->diag |= MR_D_SHADOW;
    if (ppdev->bpp >= 8) {
        mr_surf_make(ppdev, MR_BITMAP_MAX, &ppdev->scratchBits,
                     &ppdev->hsurfScratch, &ppdev->psoScratch);
    }
    mr_surf_make(ppdev, MR_PAT_SIZE, &ppdev->patBits, &ppdev->hsurfPat,
                 &ppdev->psoPat);
}

void mr_save_free(MR_SAVE *ps)
{
    if (ps->hsurf != NULL) {
        EngUnlockSurface(ps->pso);
        EngDeleteSurface(ps->hsurf);
        ps->hsurf = NULL;
        ps->pso = NULL;
    }
    ps->num = 0;
    ps->ord = MR_SV_NONE;
}

static void mr_save_unqueue(MR_PDEV *ppdev, BOOL all)
{
    ULONG i;

    for (i = 0; i < MR_SAVES; i++) {
        if (ppdev->save[i].ord == MR_SV_QUEUED ||
            (all && ppdev->save[i].ord == MR_SV_SENT)) {
            ppdev->save[i].ord = MR_SV_NONE;
        }
    }
}

void mr_shadow_free(MR_PDEV *ppdev)
{
    ULONG i;

    for (i = 0; i < MR_SAVES; i++) {
        mr_save_free(&ppdev->save[i]);
    }
    mr_surf_free(&ppdev->scratchBits, &ppdev->hsurfScratch,
                 &ppdev->psoScratch);
    mr_surf_free(&ppdev->patBits, &ppdev->hsurfPat,
                 &ppdev->psoPat);
    if (ppdev->psoShadow != NULL) {
        EngUnlockSurface(ppdev->psoShadow);
        EngDeleteSurface(ppdev->hsurfShadow);
        mr_free(ppdev->shadowBits);
        ppdev->psoShadow = NULL;
        ppdev->hsurfShadow = NULL;
        ppdev->shadowBits = NULL;
        ppdev->diag &= ~(ULONG) MR_D_SHADOW;
    }
}

static void mr_order(RECTL *pr, const RECTL *prcl)
{
    *pr = *prcl;
    if (pr->left > pr->right) {
        pr->left = prcl->right;
        pr->right = prcl->left;
    }
    if (pr->top > pr->bottom) {
        pr->top = prcl->bottom;
        pr->bottom = prcl->top;
    }
}

static void mr_clip(RECTL *pr, CLIPOBJ *pco)
{
    if (pco == NULL || pco->iDComplexity == DC_TRIVIAL) {
        return;
    }
    if (pr->left < pco->rclBounds.left) {
        pr->left = pco->rclBounds.left;
    }
    if (pr->top < pco->rclBounds.top) {
        pr->top = pco->rclBounds.top;
    }
    if (pr->right > pco->rclBounds.right) {
        pr->right = pco->rclBounds.right;
    }
    if (pr->bottom > pco->rclBounds.bottom) {
        pr->bottom = pco->rclBounds.bottom;
    }
}

static BOOL mr_fits(MR_PDEV *ppdev, const RECTL *pr)
{
    if (pr->left >= pr->right || pr->top >= pr->bottom) {
        return TRUE;
    }
    return pr->left >= 0 && pr->top >= 0 && pr->right <= ppdev->cx &&
           pr->bottom <= ppdev->cy;
}

BOOL mr_inside(MR_PDEV *ppdev, CLIPOBJ *pco, const RECTL *prcl)
{
    RECTL r;

    if (prcl != NULL) {
        r = *prcl;
    } else if (pco != NULL && pco->iDComplexity != DC_TRIVIAL) {
        r = pco->rclBounds;
    } else {
        return TRUE;
    }
    mr_clip(&r, pco);
    return mr_fits(ppdev, &r);
}

static BOOL mr_src_inside(MR_PDEV *ppdev, CLIPOBJ *pco, const RECTL *prclTrg,
                          const POINTL *pptlSrc)
{
    RECTL r;
    LONG  dx, dy;

    if (pptlSrc == NULL) {
        return FALSE;
    }
    r = *prclTrg;
    mr_clip(&r, pco);
    if (r.left >= r.right || r.top >= r.bottom) {
        return TRUE;
    }
    dx = pptlSrc->x - prclTrg->left;
    dy = pptlSrc->y - prclTrg->top;
    r.left += dx;
    r.right += dx;
    r.top += dy;
    r.bottom += dy;
    return mr_fits(ppdev, &r);
}

static BOOL mr_source(MR_PDEV *ppdev, SURFOBJ *psoSrc, SURFOBJ **ppsoOut)
{
    *ppsoOut = psoSrc;
    if (psoSrc != NULL && mr_target(psoSrc) == ppdev) {
        *ppsoOut = ppdev->psoShadow;
        return TRUE;
    }
    return psoSrc == NULL || psoSrc->iType == STYPE_BITMAP;
}

static void mr_path_rect(PATHOBJ *ppo, RECTL *pr)
{
    RECTFX rfx;

    PATHOBJ_vGetBounds(ppo, &rfx);
    pr->left = rfx.xLeft >> 4;
    pr->top = rfx.yTop >> 4;
    pr->right = ((rfx.xRight + 15) >> 4) + 1;
    pr->bottom = ((rfx.yBottom + 15) >> 4) + 1;
}

#define MR_USES_P(r)    ((((r) >> 4) & 0x0F) != ((r) & 0x0F))
#define MR_USES_S(r)    ((((r) >> 2) & 0x33) != ((r) & 0x33))

static ULONG mr_bmp_why(const RECTL *prcl, ULONG rop)
{
    LONG w = prcl->right - prcl->left, h = prcl->bottom - prcl->top;

    if (MR_USES_P(rop)) {
        return MR_RW_BMPPAT;
    }
    if (w < -64 || w > 64 || h < -64 || h > 64) {
        return MR_RW_BMPBIG;
    }
    return rop == 0xCC ? MR_RW_BMPCOPY : MR_RW_BMPROP;
}

typedef struct MR_ENUM {
    ULONG c;
    RECTL arcl[8];
} MR_ENUM;

ULONG mr_rop3(ULONG r2)
{
    ULONG r = (r2 - 1) & 0x0F;

    return (r & 3) | ((r & 3) << 2) | ((r & 0x0C) << 2) | ((r & 0x0C) << 4);
}

ULONG mr_mix(MIX mix)
{
    ULONG r2 = mix & 0xFF;

    if (r2 < 1 || r2 > 16 || ((mix >> 8) & 0xFF) != r2) {
        return 0;
    }
    return r2;
}

BOOL mr_ord_on(MR_PDEV *ppdev)
{
    return g_orders && ppdev->psoShadow != NULL &&
           (ppdev->bpp >= 8 || ppdev->bpp == 4) &&
           !ppdev->fullscreen && !(ppdev->rec.flags & MR_F_ALL);
}

static LONG mr_pieces(MR_PDEV *ppdev, CLIPOBJ *pco, const RECTL *prcl,
                      ULONG dir)
{
    MR_ORDER *po = &ppdev->ord[ppdev->nord];
    ULONG     room = MR_ORDERS - ppdev->nord, n = 0, i;
    MR_ENUM   e;
    RECTL     r, c;
    BOOL      more;

    if (room > MR_PIECES) {
        room = MR_PIECES;
    }
    r = *prcl;
    if (r.left < 0) {
        r.left = 0;
    }
    if (r.top < 0) {
        r.top = 0;
    }
    if (r.right > ppdev->cx) {
        r.right = ppdev->cx;
    }
    if (r.bottom > ppdev->cy) {
        r.bottom = ppdev->cy;
    }
    mr_clip(&r, pco);
    if (r.left >= r.right || r.top >= r.bottom) {
        return 0;
    }
    if (pco == NULL || pco->iDComplexity != DC_COMPLEX) {
        if (room < 1) {
            return -1;
        }
        po->left = r.left;
        po->top = r.top;
        po->right = r.right;
        po->bottom = r.bottom;
        return 1;
    }
    CLIPOBJ_cEnumStart(pco, FALSE, CT_RECTANGLES, dir, 0);
    do {
        more = CLIPOBJ_bEnum(pco, sizeof(e), (ULONG *) &e);
        for (i = 0; i < e.c && i < 8; i++) {
            c = e.arcl[i];
            if (c.left < r.left) {
                c.left = r.left;
            }
            if (c.top < r.top) {
                c.top = r.top;
            }
            if (c.right > r.right) {
                c.right = r.right;
            }
            if (c.bottom > r.bottom) {
                c.bottom = r.bottom;
            }
            if (c.left >= c.right || c.top >= c.bottom) {
                continue;
            }
            if (n >= room) {
                return -1;
            }
            po[n].left = c.left;
            po[n].top = c.top;
            po[n].right = c.right;
            po[n].bottom = c.bottom;
            n++;
        }
    } while (more);
    return (LONG) n;
}

static void mr_ord_add(MR_PDEV *ppdev, LONG n, ULONG kind, ULONG color,
                       ULONG rop, LONG dx, LONG dy)
{
    MR_ORDER *po = &ppdev->ord[ppdev->nord];
    BOOL      wake = n > 0 && ppdev->nord == 0;
    LONG      i;

    for (i = 0; i < n; i++) {
        po[i].kind = kind;
        po[i].sx = po[i].left - dx;
        po[i].sy = po[i].top - dy;
        po[i].color = color;
        po[i].rop = rop;
        po[i].oleft = po[i].otop = po[i].oright = po[i].obottom = 0;
        po[i].bg = 0;
        po[i].y = 0;
        po[i].first = po[i].nglyph = 0;
        if (kind == MR_O_COPY &&
            rec_move(&ppdev->rec, po[i].left, po[i].top, po[i].right,
                     po[i].bottom, dx, dy)) {
            wake = TRUE;
        }
    }
    if (wake) {
        mr_signal();
    }
    ppdev->nord += (ULONG) n;
    g_ordcalls++;
}

static BOOL mr_ord_one(MR_PDEV *ppdev, ULONG kind, const RECTL *pr,
                       ULONG num)
{
    MR_ORDER *po;

    if (ppdev->nord >= MR_ORDERS) {
        return FALSE;
    }
    po = &ppdev->ord[ppdev->nord];
    mr_zero(po, sizeof(*po));
    po->kind = kind;
    po->left = pr->left;
    po->top = pr->top;
    po->right = pr->right;
    po->bottom = pr->bottom;
    po->first = num;
    if (ppdev->nord++ == 0) {
        mr_signal();
    }
    return TRUE;
}

static ULONG mr_pixel(const BYTE *row, ULONG fmt, LONG x);
static BOOL  mr_same(const BYTE *a, const BYTE *b, ULONG n);

static ULONG mr_brush(MR_PDEV *ppdev, BRUSHOBJ *pbo, BYTE pat[8],
                      ULONG *pc0, ULONG *pc1)
{
    SURFOBJ    *pso = ppdev->psoPat;
    const BYTE *top, *row;
    RECTL       r;
    POINTL      org;
    ULONG       step, rowb, c, n = 1;
    LONG        x, y;

    if (pso == NULL) {
        return 0;
    }
    switch (pso->iBitmapFormat) {
    case BMF_1BPP:
        step = 1;
        break;
    case BMF_4BPP:
        step = 4;
        break;
    case BMF_8BPP:
        step = 8;
        break;
    case BMF_16BPP:
        step = 16;
        break;
    case BMF_24BPP:
        step = 24;
        break;
    case BMF_32BPP:
        step = 32;
        break;
    default:
        return 0;
    }
    rowb = MR_PAT_SIZE / 8 * step;
    r.left = 0;
    r.top = 0;
    r.right = MR_PAT_SIZE;
    r.bottom = MR_PAT_SIZE;
    org.x = 0;
    org.y = 0;
    if (!EngBitBlt(pso, NULL, NULL, NULL, NULL, &r, NULL, NULL, pbo, &org,
                   0xF0F0)) {
        return 0;
    }
    top = (const BYTE *) pso->pvScan0;
    for (y = 0; y < MR_PAT_SIZE; y++) {
        row = top + y * pso->lDelta;
        if (!mr_same(row + step, row, rowb - step) ||
            (y >= 8 && !mr_same(row, top + (y & 7) * pso->lDelta, rowb))) {
            return 0;
        }
    }
    *pc0 = mr_pixel(top, pso->iBitmapFormat, 0);
    *pc1 = *pc0;
    for (y = 0; y < 8; y++) {
        row = top + y * pso->lDelta;
        pat[y] = 0;
        for (x = 0; x < 8; x++) {
            c = mr_pixel(row, pso->iBitmapFormat, x);
            if (c == *pc0) {
                continue;
            }
            if (n == 1) {
                *pc1 = c;
                n = 2;
            } else if (c != *pc1) {
                return 0;
            }
            pat[y] |= (BYTE) (0x80 >> x);
        }
    }
    return n;
}

ULONG mr_fill_order(MR_PDEV *ppdev, CLIPOBJ *pco, const RECTL *prcl,
                    BRUSHOBJ *pbo, const POINTL *pptlBrush, ULONG rop,
                    ULONG kind, ULONG r2)
{
    MR_ORDER *po;
    ULONG     color = 0, bg = 0, np = 0, lo, hi;
    BYTE      pat[8];
    LONG      n, i;

    if (MR_USES_S(rop)) {
        return MR_RW_OTHER;
    }
    if (MR_USES_P(rop)) {
        if (pbo == NULL) {
            return MR_RW_PATTERN;
        }
        if (pbo->iSolidColor != 0xFFFFFFFF) {
            color = pbo->iSolidColor;
        } else if (!g_patterns || kind != MR_O_FILL || pptlBrush == NULL) {
            return MR_RW_PATTERN;
        } else {
            np = 1;
        }
    }
    n = mr_pieces(ppdev, pco, prcl, CD_ANY);
    if (n < 0) {
        g_ordmiss++;
        return MR_RW_FULL;
    }
    if (np != 0 && n > 0) {
        np = mr_brush(ppdev, pbo, pat, &color, &bg);
        if (np == 0) {
            return MR_RW_PATTERN;
        }
    }
    po = &ppdev->ord[ppdev->nord];
    if (np == 2) {
        mr_ord_add(ppdev, n, MR_O_PATTERN, color, rop, 0, 0);
        lo = pat[0] | ((ULONG) pat[1] << 8) | ((ULONG) pat[2] << 16) |
             ((ULONG) pat[3] << 24);
        hi = pat[4] | ((ULONG) pat[5] << 8) | ((ULONG) pat[6] << 16) |
             ((ULONG) pat[7] << 24);
        for (i = 0; i < n; i++) {
            po[i].sx = (LONG) ((ULONG) pptlBrush->x & 7);
            po[i].sy = (LONG) ((ULONG) pptlBrush->y & 7);
            po[i].bg = bg;
            po[i].first = lo;
            po[i].nglyph = hi;
        }
    } else {
        mr_ord_add(ppdev, n, kind, color, kind == MR_O_LINE ? r2 : rop,
                   0, 0);
    }
    if (np != 0) {
        g_patcalls++;
    }
    return MR_DONE;
}

static ULONG mr_copy_order(MR_PDEV *ppdev, CLIPOBJ *pco,
                           const RECTL *prcl, const POINTL *pptlSrc)
{
    LONG  dx = prcl->left - pptlSrc->x, dy = prcl->top - pptlSrc->y, n;
    ULONG dir;

    if (prcl->left >= prcl->right || prcl->top >= prcl->bottom) {
        return MR_RW_OTHER;
    }
    if (dy > 0) {
        dir = dx > 0 ? CD_LEFTUP : CD_RIGHTUP;
    } else {
        dir = dx > 0 ? CD_LEFTDOWN : CD_RIGHTDOWN;
    }
    n = mr_pieces(ppdev, pco, prcl, dir);
    if (n < 0) {
        g_ordmiss++;
        return MR_RW_FULL;
    }
    mr_ord_add(ppdev, n, MR_O_COPY, 0, 0xCC, dx, dy);
    return MR_DONE;
}

static ULONG mr_fnv(ULONG h, const BYTE *p, ULONG n)
{
    while (n != 0) {
        h = (h ^ *p++) * 16777619UL;
        n--;
    }
    return h;
}

static BOOL mr_same(const BYTE *a, const BYTE *b, ULONG n)
{
    while (n != 0) {
        if (*a++ != *b++) {
            return FALSE;
        }
        n--;
    }
    return TRUE;
}

static ULONG mr_bmp_order(MR_PDEV *ppdev, SURFOBJ *psoS, CLIPOBJ *pco,
                          XLATEOBJ *pxlo, const RECTL *prcl,
                          POINTL *pptlSrc, ULONG rop)
{
    LONG        w = prcl->right - prcl->left, h = prcl->bottom - prcl->top;
    LONG        n, i;
    ULONG       rowb, size, slot, off;
    RECTL       rs;
    BYTE       *dst;
    const BYTE *src, *q;
    MR_BITMAP  *pb;
    MR_ORDER   *po;

    if (!g_bitmaps || ppdev->psoScratch == NULL) {
        return mr_bmp_why(prcl, rop);
    }
    if (w <= 0 || h <= 0 || pptlSrc == NULL || psoS == NULL) {
        return MR_RW_OTHER;
    }
    if (w > MR_BITMAP_MAX || h > MR_BITMAP_MAX) {
        return MR_RW_BMPBIG;
    }
    if (pptlSrc->x < 0 || pptlSrc->y < 0 ||
        pptlSrc->x > psoS->sizlBitmap.cx - w ||
        pptlSrc->y > psoS->sizlBitmap.cy - h) {
        return MR_RW_OTHER;
    }
    rowb = MR_ROW_BYTES(0, w, ppdev->bpp);
    size = MR_BITMAP_SIZE(w, h, ppdev->bpp);
    if (ppdev->nbmp >= MR_BITMAPS || size > MR_BITMAPAREA - ppdev->bmplen) {
        g_ordmiss++;
        return MR_RW_FULL;
    }
    n = mr_pieces(ppdev, pco, prcl, CD_ANY);
    if (n < 0) {
        g_ordmiss++;
        return MR_RW_FULL;
    }
    if (n == 0) {
        return MR_DONE;
    }
    rs.left = 0;
    rs.top = 0;
    rs.right = w;
    rs.bottom = h;
    if (!EngCopyBits(ppdev->psoScratch, psoS, NULL, pxlo, &rs, pptlSrc)) {
        return MR_RW_OTHER;
    }
    off = ppdev->bmplen;
    pb = (MR_BITMAP *) (ppdev->bitmaps + off);
    pb->cx = (ULONG) w;
    pb->cy = (ULONG) h;
    dst = (BYTE *) (pb + 1);
    src = (const BYTE *) ppdev->psoScratch->pvScan0;
    for (i = 0; i < h; i++) {
        mr_copy(dst, src, rowb);
        dst += rowb;
        src += ppdev->psoScratch->lDelta;
    }
    mr_zero(dst, size - sizeof(MR_BITMAP) - rowb * (ULONG) h);
    for (slot = mr_fnv(2166136261UL, (const BYTE *) pb, size) &
                (MR_BHASH - 1);
         ppdev->bhash[slot] != 0; slot = (slot + 1) & (MR_BHASH - 1)) {
        q = ppdev->bitmaps + ppdev->bhash[slot] - 1;
        if (MR_BITMAP_SIZE(((const MR_BITMAP *) q)->cx,
                           ((const MR_BITMAP *) q)->cy, ppdev->bpp) == size &&
            mr_same(q, (const BYTE *) pb, size)) {
            off = ppdev->bhash[slot] - 1;
            break;
        }
    }
    if (ppdev->bhash[slot] == 0) {
        ppdev->bhash[slot] = off + 1;
        ppdev->bmplen += size;
        ppdev->nbmp++;
    }
    po = &ppdev->ord[ppdev->nord];
    mr_ord_add(ppdev, n, MR_O_BITMAP, 0, rop, prcl->left, prcl->top);
    for (i = 0; i < n; i++) {
        po[i].first = off;
    }
    g_bmpcalls++;
    return MR_DONE;
}

#define MR_NONE 0xFFFFFFFFUL

static void mr_grow(RECTL *pu, int *phave, const RECTL *prcl);

static void mr_data_reset(MR_PDEV *ppdev)
{
    if (ppdev->glyphlen > 0) {
        mr_zero(ppdev->ghash, sizeof(ppdev->ghash));
    }
    ppdev->nplace = 0;
    ppdev->glyphlen = 0;
    if (ppdev->nbmp > 0) {
        mr_zero(ppdev->bhash, sizeof(ppdev->bhash));
    }
    ppdev->nbmp = 0;
    ppdev->bmplen = 0;
}

static ULONG mr_ghash(const MR_GLYPH *pg, const BYTE *bits, ULONG nb)
{
    ULONG h = 2166136261UL, i;

    h = (h ^ (ULONG) pg->ox) * 16777619UL;
    h = (h ^ (ULONG) pg->oy) * 16777619UL;
    h = (h ^ pg->cx) * 16777619UL;
    h = (h ^ pg->cy) * 16777619UL;
    for (i = 0; i < nb; i++) {
        h = (h ^ bits[i]) * 16777619UL;
    }
    return h;
}

static ULONG mr_gfind(MR_PDEV *ppdev, const MR_GLYPH *pg, const BYTE *bits,
                      ULONG nb, ULONG *pslot)
{
    ULONG           s = mr_ghash(pg, bits, nb) & (MR_GHASH - 1), off, i;
    const MR_GLYPH *q;
    const BYTE     *qb;

    while (ppdev->ghash[s] != 0) {
        off = ppdev->ghash[s] - 1;
        q = (const MR_GLYPH *) (ppdev->glyphs + off);
        if (q->ox == pg->ox && q->oy == pg->oy && q->cx == pg->cx &&
            q->cy == pg->cy) {
            qb = (const BYTE *) (q + 1);
            for (i = 0; i < nb && qb[i] == bits[i]; i++) {
            }
            if (i == nb) {
                *pslot = s;
                return off;
            }
        }
        s = (s + 1) & (MR_GHASH - 1);
    }
    *pslot = s;
    return MR_NONE;
}

static int mr_glyph_at(STROBJ *pstro, GLYPHPOS *pgp, ULONG i, BOOL tops,
                       LONG top, LONG y0, MR_GLYPH *pgh, const BYTE **pbits,
                       ULONG *pnb, LONG *px)
{
    GLYPHBITS *pgb;
    ULONG      rowb, nb, j;
    LONG       cx, cy, y;

    if (pgp[i].pgdf == NULL || pgp[i].pgdf->pgb == NULL) {
        return -1;
    }
    pgb = pgp[i].pgdf->pgb;
    cx = pgb->sizlBitmap.cx;
    cy = pgb->sizlBitmap.cy;
    if (cx < 1 || cy < 1 || cx > 8 * MR_GLYPH_BITS || cy > MR_GLYPH_BITS) {
        return -1;
    }
    rowb = MR_GLYPH_ROWB(cx);
    nb = rowb * (ULONG) cy;
    if (nb > MR_GLYPH_BITS) {
        return -1;
    }
    if (cx % 8 != 0) {
        for (j = 0; j < (ULONG) cy; j++) {
            if (pgb->aj[j * rowb + rowb - 1] & (0xFF >> (cx % 8))) {
                return -1;
            }
        }
    }
    if (pstro->ulCharInc != 0) {
        *px = pgp[0].ptl.x + (LONG) (i * pstro->ulCharInc);
        y = pgp[0].ptl.y;
    } else {
        *px = pgp[i].ptl.x;
        y = pgp[i].ptl.y;
    }
    pgh->ox = pgb->ptlOrigin.x;
    pgh->oy = (tops ? top : y + pgb->ptlOrigin.y) - y0;
    pgh->cx = (ULONG) cx;
    pgh->cy = (ULONG) cy;
    *pbits = pgb->aj;
    *pnb = nb;
    for (j = 0; j < nb; j++) {
        if (pgb->aj[j] != 0) {
            return 1;
        }
    }
    return 0;
}

static BOOL mr_text_miss(ULONG why)
{
    g_textmiss++;
    g_textwhy[why]++;
    return FALSE;
}

static BOOL mr_text_order(MR_PDEV *ppdev, STROBJ *pstro, FONTOBJ *pfo,
    CLIPOBJ *pco, RECTL *prclExtra, const RECTL *popq, BRUSHOBJ *pboFore,
    BRUSHOBJ *pboOpaque, const RECTL *ptb)
{
    GLYPHPOS   *pgp;
    MR_GLYPH    gh;
    MR_ORDER   *po;
    MR_GLYPH   *pg;
    const BYTE *bits;
    RECTL       area, cell;
    ULONG       c, i, nb, need = 0, nsend = 0, slot, off, first, all, size;
    LONG        x, y0, xprev = 0, right = 0, n, k;
    BOOL        more, tops, wake;
    int         have = 0, kind;

    all = SO_ZERO_BEARINGS | SO_FLAG_DEFAULT_PLACEMENT |
          SO_MAXEXT_EQUAL_BM_SIDE;
    if (!(pstro->flAccel & SO_HORIZONTAL) ||
        (pstro->flAccel & SO_REVERSED) || (pfo->flFontType & FO_GRAY16) ||
        pboFore == NULL || pboFore->iSolidColor == 0xFFFFFFFF ||
        (popq != NULL && (pboOpaque == NULL ||
                          pboOpaque->iSolidColor == 0xFFFFFFFF)) ||
        (prclExtra != NULL && prclExtra[0].left != prclExtra[0].right)) {
        return mr_text_miss(MR_TM_KIND);
    }
    if (popq != NULL && (popq->left > ptb->left || popq->top > ptb->top ||
                         popq->right < ptb->right ||
                         popq->bottom < ptb->bottom)) {
        return mr_text_miss(MR_TM_OPAQUE);
    }
#ifdef MR_LAYER
    tops = FALSE;
#else
    tops = (pstro->flAccel & all) == all;
#endif
    STROBJ_vEnumStart(pstro);
    if (pstro->pgp != NULL) {
        pgp = pstro->pgp;
        c = pstro->cGlyphs;
        more = FALSE;
    } else {
        more = STROBJ_bEnum(pstro, &c, &pgp);
    }
    if (more || c > MR_TEXT_MAX) {
        STROBJ_vEnumStart(pstro);
        return mr_text_miss(MR_TM_BATCH);
    }
    if (c < 1) {
        STROBJ_vEnumStart(pstro);
        return mr_text_miss(MR_TM_OTHER);
    }
    y0 = pgp[0].ptl.y;
    for (i = 0; i < c; i++) {
        kind = mr_glyph_at(pstro, pgp, i, tops, ptb->top, y0, &gh, &bits,
                           &nb, &x);
        if (kind < 0) {
            STROBJ_vEnumStart(pstro);
            return mr_text_miss(MR_TM_GLYPH);
        }
        cell.left = x + gh.ox;
        cell.top = y0 + gh.oy;
        cell.right = cell.left + (LONG) gh.cx;
        cell.bottom = cell.top + (LONG) gh.cy;
        if ((i > 0 && x < xprev) || (tops && i > 0 && cell.left < right)) {
            STROBJ_vEnumStart(pstro);
            return mr_text_miss(MR_TM_PLACE);
        }
        xprev = x;
        if (i == 0 || cell.right > right) {
            right = cell.right;
        }
        if (kind == 0) {
            continue;
        }
#ifdef MR_LAYER
        if (cell.left < ptb->left || cell.top < ptb->top ||
            cell.right > ptb->right || cell.bottom > ptb->bottom) {
            STROBJ_vEnumStart(pstro);
            return mr_text_miss(MR_TM_PLACE);
        }
#endif
        mr_grow(&area, &have, &cell);
        if (mr_gfind(ppdev, &gh, bits, nb, &slot) == MR_NONE) {
            need += MR_GLYPH_SIZE(gh.cx, gh.cy);
        }
        nsend++;
    }
    if (nsend == 0) {
        STROBJ_vEnumStart(pstro);
        if (popq == NULL) {
            g_textcalls++;
            return TRUE;
        }
        n = mr_pieces(ppdev, pco, popq, CD_ANY);
        if (n < 0) {
            return mr_text_miss(MR_ORDERS - ppdev->nord < MR_PIECES ?
                                MR_TM_FULL : MR_TM_PIECES);
        }
        mr_ord_add(ppdev, n, MR_O_FILL, pboOpaque->iSolidColor, 0xF0, 0, 0);
        g_textcalls++;
        return TRUE;
    }
    if (nsend > MR_PLACES - ppdev->nplace ||
        need > MR_GLYPHAREA - ppdev->glyphlen) {
        STROBJ_vEnumStart(pstro);
        return mr_text_miss(MR_TM_FULL);
    }
    if (popq != NULL) {
        mr_grow(&area, &have, popq);
    }
    n = mr_pieces(ppdev, pco, &area, CD_ANY);
    if (n < 0) {
        STROBJ_vEnumStart(pstro);
        return mr_text_miss(MR_ORDERS - ppdev->nord < MR_PIECES ?
                            MR_TM_FULL : MR_TM_PIECES);
    }
    if (n == 0) {
        STROBJ_vEnumStart(pstro);
        g_textcalls++;
        return TRUE;
    }
    first = ppdev->nplace;
    for (i = 0; i < c; i++) {
        if (mr_glyph_at(pstro, pgp, i, tops, ptb->top, y0, &gh, &bits, &nb,
                        &x) != 1) {
            continue;
        }
        off = mr_gfind(ppdev, &gh, bits, nb, &slot);
        if (off == MR_NONE) {
            off = ppdev->glyphlen;
            size = MR_GLYPH_SIZE(gh.cx, gh.cy);
            pg = (MR_GLYPH *) (ppdev->glyphs + off);
            mr_zero(pg, size);
            *pg = gh;
            mr_copy(pg + 1, bits, nb);
            ppdev->glyphlen += size;
            ppdev->ghash[slot] = off + 1;
        }
        ppdev->place[ppdev->nplace].glyph = off;
        ppdev->place[ppdev->nplace].x = x;
        ppdev->nplace++;
    }
    STROBJ_vEnumStart(pstro);
    wake = ppdev->nord == 0;
    po = &ppdev->ord[ppdev->nord];
    for (k = 0; k < n; k++) {
        po[k].kind = MR_O_TEXT;
        po[k].sx = po[k].sy = 0;
        po[k].color = pboFore->iSolidColor;
        po[k].rop = 0;
        if (popq != NULL) {
            po[k].oleft = popq->left;
            po[k].otop = popq->top;
            po[k].oright = popq->right;
            po[k].obottom = popq->bottom;
            po[k].bg = pboOpaque->iSolidColor;
        } else {
            po[k].oleft = po[k].otop = po[k].oright = po[k].obottom = 0;
            po[k].bg = 0;
        }
        po[k].y = y0;
        po[k].first = first;
        po[k].nglyph = nsend;
    }
    ppdev->nord += (ULONG) n;
    g_ordcalls++;
    g_textcalls++;
    if (wake) {
        mr_signal();
    }
    return TRUE;
}

static void mr_ord_drop(MR_PDEV *ppdev)
{
    ULONG i;

    for (i = 0; i < ppdev->nord; i++) {
        if (ppdev->ord[i].kind == MR_O_SAVE) {
            continue;
        }
        rec_add(&ppdev->rec, ppdev->ord[i].left, ppdev->ord[i].top,
                ppdev->ord[i].right, ppdev->ord[i].bottom);
    }
    ppdev->nord = 0;
    mr_save_unqueue(ppdev, FALSE);
    mr_data_reset(ppdev);
    g_orders = 0;
    g_text = 0;
    g_bitmaps = 0;
    g_patterns = 0;
}

ULONG mr_save(MR_PDEV *ppdev, const RECTL *prcl)
{
    RECTL    r, rd;
    POINTL   pt;
    SIZEL    sizl;
    HBITMAP  hbm;
    SURFOBJ *pso = NULL;
    ULONG    i;

    if (prcl == NULL) {
        return 0;
    }
    mr_order(&r, prcl);
    if (r.left < 0) {
        r.left = 0;
    }
    if (r.top < 0) {
        r.top = 0;
    }
    if (r.right > ppdev->cx) {
        r.right = ppdev->cx;
    }
    if (r.bottom > ppdev->cy) {
        r.bottom = ppdev->cy;
    }
    if (r.left >= r.right || r.top >= r.bottom) {
        return 0;
    }
    for (i = 0; i < MR_SAVES && ppdev->save[i].hsurf != NULL; i++) {
    }
    if (i == MR_SAVES) {
        return 0;
    }
    sizl.cx = r.right - r.left;
    sizl.cy = r.bottom - r.top;
    hbm = EngCreateBitmap(sizl, 0, ppdev->iFormat, BMF_TOPDOWN, NULL);
    if (hbm != NULL) {
        pso = EngLockSurface((HSURF) hbm);
    }
    if (pso == NULL) {
        if (hbm != NULL) {
            EngDeleteSurface((HSURF) hbm);
        }
        return 0;
    }
    rd.left = 0;
    rd.top = 0;
    rd.right = sizl.cx;
    rd.bottom = sizl.cy;
    pt.x = r.left;
    pt.y = r.top;
    if (!EngCopyBits(pso, ppdev->psoShadow, NULL, NULL, &rd, &pt)) {
        EngUnlockSurface(pso);
        EngDeleteSurface((HSURF) hbm);
        return 0;
    }
    ppdev->save[i].hsurf = (HSURF) hbm;
    ppdev->save[i].pso = pso;
    ppdev->save[i].rcl = r;
    return i + 1;
}

void mr_save_order(MR_PDEV *ppdev, MR_SAVE *ps)
{
    if (++g_savenum == 0) {
        g_savenum = 1;
    }
    ps->num = g_savenum;
    ps->ord = MR_SV_NONE;
    if (!mr_ord_on(ppdev)) {
        return;
    }
    rec_bounds(&ppdev->rec, ps->rcl.left, ps->rcl.top, ps->rcl.right,
               ps->rcl.bottom, &ps->stale);
    if (mr_ord_one(ppdev, MR_O_SAVE, &ps->rcl, ps->num)) {
        ps->ord = MR_SV_QUEUED;
        g_saveords++;
    }
}

BOOL mr_restore_area(MR_PDEV *ppdev, ULONG ident, const RECTL *prcl,
                     RECTL *prd)
{
    MR_SAVE *ps;

    prd->left = prd->top = prd->right = prd->bottom = 0;
    if (ident == 0 || ident > MR_SAVES) {
        return FALSE;
    }
    ps = &ppdev->save[ident - 1];
    if (ps->hsurf == NULL) {
        return FALSE;
    }
    if (prcl != NULL) {
        mr_order(prd, prcl);
        if (prd->left < 0) {
            prd->left = 0;
        }
        if (prd->top < 0) {
            prd->top = 0;
        }
        if (prd->right > ppdev->cx) {
            prd->right = ppdev->cx;
        }
        if (prd->bottom > ppdev->cy) {
            prd->bottom = ppdev->cy;
        }
    } else {
        *prd = ps->rcl;
    }
    return prd->left >= ps->rcl.left && prd->top >= ps->rcl.top &&
           prd->right <= ps->rcl.right && prd->bottom <= ps->rcl.bottom &&
           prd->left < prd->right && prd->top < prd->bottom;
}

BOOL mr_restore(MR_PDEV *ppdev, ULONG ident, const RECTL *prcl)
{
    MR_SAVE *ps;
    RECTL    rd;
    POINTL   pt;
    BOOL     ok = FALSE;

    if (ident == 0 || ident > MR_SAVES) {
        return FALSE;
    }
    ps = &ppdev->save[ident - 1];
    if (ps->hsurf == NULL) {
        return FALSE;
    }
    if (mr_restore_area(ppdev, ident, prcl, &rd)) {
        pt.x = rd.left - ps->rcl.left;
        pt.y = rd.top - ps->rcl.top;
        ok = EngCopyBits(ppdev->psoShadow, ps->pso, NULL, NULL, &rd, &pt);
    }
    if (ok && ps->ord != MR_SV_NONE && rd.left == ps->rcl.left &&
        rd.top == ps->rcl.top && rd.right == ps->rcl.right &&
        rd.bottom == ps->rcl.bottom && mr_ord_on(ppdev) &&
        mr_ord_one(ppdev, MR_O_RESTORE, &rd, ps->num)) {
        g_restords++;
        if (rec_add(&ppdev->rec, ps->stale.left, ps->stale.top,
                    ps->stale.right, ps->stale.bottom)) {
            mr_signal();
        }
    } else {
        mr_rect(ppdev, NULL, rd.left, rd.top, rd.right, rd.bottom,
                MR_RW_RESTORE);
    }
    mr_save_free(ps);
    return ok;
}

VOID mr_start_reset(MR_PDEV *ppdev)
{
    ULONG i;

    g_orders = 0;
    g_text = 0;
    g_bitmaps = 0;
    g_patterns = 0;
    ppdev->nord = 0;
    mr_data_reset(ppdev);
    for (i = 0; i < MR_MAXPDEV; i++) {
        if (g_pdev[i] != NULL) {
            mr_save_unqueue(g_pdev[i], TRUE);
        }
    }
    rec_flag(&ppdev->rec, MR_F_ALL);
    mr_signal();
}

VOID mr_stop_reset(VOID)
{
    ULONG i;

    g_orders = 0;
    g_text = 0;
    g_bitmaps = 0;
    g_patterns = 0;
    for (i = 0; i < MR_MAXPDEV; i++) {
        if (g_pdev[i] != NULL) {
            g_pdev[i]->nord = 0;
            mr_data_reset(g_pdev[i]);
            mr_save_unqueue(g_pdev[i], TRUE);
        }
    }
}

static ULONG mr_get(MR_PDEV *ppdev, MR_OUT *pout)
{
    ULONG st = mr_chan_check();

    if (st != MR_OK) {
        return st;
    }
    mr_ord_drop(ppdev);
    rec_take(&ppdev->rec, pout);
    return MR_OK;
}

static ULONG mr_flush(MR_PDEV *ppdev, MR_FLUSHOUT *pf, ULONG cjOut,
                      ULONG arg)
{
    SURFOBJ     *pso = ppdev->psoShadow;
    MR_ORDER    *po = (MR_ORDER *) (pf + 1);
    MR_GLYPHPOS *pp;
    BYTE        *dst, *pix;
    const BYTE  *src;
    MR_RECT     *pr;
    ULONG        i, k, flags, rowb, xb, nord, nplace, glen, blen;
    LONG         y;

    k = mr_chan_check();
    if (k != MR_OK) {
        return k;
    }
    if (pso == NULL) {
        return MR_NOSHADOW;
    }
    if (!(arg & MR_FL_ORDERS)) {
        mr_ord_drop(ppdev);
    } else {
        if (ppdev->rec.flags & MR_F_ALL) {
            ppdev->nord = 0;
            mr_save_unqueue(ppdev, FALSE);
            mr_data_reset(ppdev);
        }
        g_orders = 1;
        g_text = (arg & MR_FL_TEXT) ? 1 : 0;
        g_bitmaps = (arg & MR_FL_BITMAPS) ? 1 : 0;
        g_patterns = (arg & MR_FL_PATTERNS) ? 1 : 0;
    }
    nord = ppdev->nord;
    nplace = ppdev->nplace;
    glen = ppdev->glyphlen;
    blen = ppdev->bmplen;
    if (nord > 0) {
        mr_copy(po, ppdev->ord, nord * sizeof(MR_ORDER));
    }
    pp = (MR_GLYPHPOS *) (po + nord);
    if (nplace > 0) {
        mr_copy(pp, ppdev->place, nplace * sizeof(MR_GLYPHPOS));
    }
    dst = (BYTE *) (pp + nplace);
    if (glen > 0) {
        mr_copy(dst, ppdev->glyphs, glen);
    }
    if (blen > 0) {
        mr_copy(dst + glen, ppdev->bitmaps, blen);
    }
    ppdev->nord = 0;
    mr_data_reset(ppdev);
    pf->norders = nord;
    pf->ordcalls = g_ordcalls;
    pf->ordmiss = g_ordmiss;
    pf->nplaces = nplace;
    pf->glyphlen = glen;
    pf->textcalls = g_textcalls;
    pf->textmiss = g_textmiss;
    for (i = 0; i < 8; i++) {
        pf->textwhy[i] = g_textwhy[i];
    }
    for (i = 0; i < MR_RW_N; i++) {
        pf->rectwhy[i] = g_rectwhy[i];
        pf->rectpix[i] = g_rectpix[i];
    }
    for (i = 0; i < MR_SAVES; i++) {
        if (ppdev->save[i].ord == MR_SV_QUEUED) {
            ppdev->save[i].ord = MR_SV_SENT;
        }
        pf->savenum[i] = ppdev->save[i].ord == MR_SV_SENT ?
                         ppdev->save[i].num : 0;
    }
    pf->saveords = g_saveords;
    pf->restords = g_restords;
    pf->bitmaplen = blen;
    pf->bmpcalls = g_bmpcalls;
    pf->patcalls = g_patcalls;
    dst += glen + blen;
    pix = dst;
    k = rec_flush(&ppdev->rec, ppdev->bpp,
                  cjOut - sizeof(MR_FLUSHOUT) - nord * sizeof(MR_ORDER) -
                  nplace * sizeof(MR_GLYPHPOS) - glen - blen,
                  pf->head.rect, &flags);
    pf->head.flags |= flags;
    pf->head.nrects = k;
    for (i = 0; i < k; i++) {
        pr = &pf->head.rect[i];
        rowb = MR_ROW_BYTES(pr->left, pr->right, ppdev->bpp);
        xb = (ULONG) pr->left * ppdev->bpp / 8;
        for (y = pr->top; y < pr->bottom; y++) {
            src = (const BYTE *) pso->pvScan0 + y * pso->lDelta + xb;
            mr_copy(dst, src, rowb);
            dst += rowb;
        }
    }
    pf->datalen = (ULONG) (dst - pix);
    return MR_OK;
}

static ULONG mr_blt_order(MR_PDEV *ppdev, SURFOBJ *psoS, CLIPOBJ *pco,
    XLATEOBJ *pxlo, RECTL *prclTrg, POINTL *pptlSrc, BRUSHOBJ *pbo,
    POINTL *pptlBrush, ROP4 rop4)
{
    ULONG rop = rop4 & 0xFF;

    if (((rop4 >> 8) & 0xFF) != rop) {
        return MR_RW_MASK;
    }
    if (MR_USES_S(rop)) {
        if (psoS != ppdev->psoShadow) {
            if (MR_USES_P(rop)) {
                return MR_RW_BMPPAT;
            }
            return mr_bmp_order(ppdev, psoS, pco, pxlo, prclTrg, pptlSrc,
                                rop);
        }
        if (rop != 0xCC || pptlSrc == NULL ||
            (pxlo != NULL && !(pxlo->flXlate & XO_TRIVIAL))) {
            return MR_RW_MASK;
        }
        return mr_copy_order(ppdev, pco, prclTrg, pptlSrc);
    }
    return mr_fill_order(ppdev, pco, prclTrg, pbo, pptlBrush, rop,
                         MR_O_FILL, 0);
}

BOOL APIENTRY mrBitBlt(SURFOBJ *psoTrg, SURFOBJ *psoSrc,
    SURFOBJ *psoMask, CLIPOBJ *pco, XLATEOBJ *pxlo, RECTL *prclTrg,
    POINTL *pptlSrc, POINTL *pptlMask, BRUSHOBJ *pbo, POINTL *pptlBrush,
    ROP4 rop4)
{
    MR_PDEV *ppdev = mr_target(psoTrg);
    SURFOBJ *psoS;
    ULONG    why = MR_RW_OTHER;

    mr_count_read(psoSrc, ppdev);
    if (ppdev == NULL) {
        return TRUE;
    }
    if (ppdev->psoShadow != NULL) {
        if (prclTrg != NULL && mr_source(ppdev, psoSrc, &psoS) &&
            (psoMask == NULL || psoMask->iType == STYPE_BITMAP) &&
            mr_inside(ppdev, pco, prclTrg) &&
            (psoS != ppdev->psoShadow ||
             mr_src_inside(ppdev, pco, prclTrg, pptlSrc))) {
            if (!EngBitBlt(ppdev->psoShadow, psoS, psoMask, pco, pxlo,
                           prclTrg, pptlSrc, pptlMask, pbo, pptlBrush,
                           rop4)) {
                g_skipped++;
            } else if (!mr_ord_on(ppdev)) {
                why = MR_RW_OFF;
            } else if ((why = mr_blt_order(ppdev, psoS, pco, pxlo, prclTrg,
                                           pptlSrc, pbo, pptlBrush,
                                           rop4)) == MR_DONE) {
                return TRUE;
            }
        } else {
            g_skipped++;
        }
    }
    mr_rcl(ppdev, pco, prclTrg, why);
    return TRUE;
}

BOOL APIENTRY mrCopyBits(SURFOBJ *psoDest, SURFOBJ *psoSrc,
    CLIPOBJ *pco, XLATEOBJ *pxlo, RECTL *prclDest, POINTL *pptlSrc)
{
    MR_PDEV *ppdev = mr_target(psoDest);
    SURFOBJ *psoS;
    ULONG    why = MR_RW_OTHER;

    mr_count_read(psoSrc, ppdev);
    if (ppdev == NULL) {
        return TRUE;
    }
    if (ppdev->psoShadow != NULL) {
        if (prclDest != NULL && pptlSrc != NULL &&
            mr_source(ppdev, psoSrc, &psoS) && psoS != NULL &&
            mr_inside(ppdev, pco, prclDest) &&
            (psoS != ppdev->psoShadow ||
             mr_src_inside(ppdev, pco, prclDest, pptlSrc))) {
            if (!EngCopyBits(ppdev->psoShadow, psoS, pco, pxlo, prclDest,
                             pptlSrc)) {
                g_skipped++;
            } else if (!mr_ord_on(ppdev)) {
                why = MR_RW_OFF;
            } else if (psoS != ppdev->psoShadow) {
                if ((why = mr_bmp_order(ppdev, psoS, pco, pxlo, prclDest,
                                        pptlSrc, 0xCC)) == MR_DONE) {
                    return TRUE;
                }
            } else if (pxlo != NULL && !(pxlo->flXlate & XO_TRIVIAL)) {
                why = MR_RW_MASK;
            } else if ((why = mr_copy_order(ppdev, pco, prclDest,
                                            pptlSrc)) == MR_DONE) {
                return TRUE;
            }
        } else {
            g_skipped++;
        }
    }
    mr_rcl(ppdev, pco, prclDest, why);
    return TRUE;
}

BOOL APIENTRY mrStretchBlt(SURFOBJ *psoDest, SURFOBJ *psoSrc,
    SURFOBJ *psoMask, CLIPOBJ *pco, XLATEOBJ *pxlo, COLORADJUSTMENT *pca,
    POINTL *pptlHTOrg, RECTL *prclDest, RECTL *prclSrc, POINTL *pptlMask,
    ULONG iMode)
{
    MR_PDEV *ppdev = mr_target(psoDest);
    SURFOBJ *psoS;
    RECTL    rd, rs;

    mr_count_read(psoSrc, ppdev);
    if (ppdev == NULL) {
        return TRUE;
    }
    if (ppdev->psoShadow != NULL) {
        if (prclDest != NULL && prclSrc != NULL) {
            mr_order(&rd, prclDest);
            mr_order(&rs, prclSrc);
        }
        if (prclDest != NULL && prclSrc != NULL &&
            mr_source(ppdev, psoSrc, &psoS) && psoS != NULL &&
            (psoMask == NULL || psoMask->iType == STYPE_BITMAP) &&
            mr_inside(ppdev, pco, &rd) &&
            (psoS != ppdev->psoShadow || mr_fits(ppdev, &rs))) {
            if (!EngStretchBlt(ppdev->psoShadow, psoS, psoMask, pco, pxlo,
                               pca, pptlHTOrg, prclDest, prclSrc, pptlMask,
                               iMode)) {
                g_skipped++;
            }
        } else {
            g_skipped++;
        }
    }
    mr_rcl(ppdev, pco, prclDest, MR_RW_SHAPE);
    return TRUE;
}

static void mr_grow(RECTL *pu, int *phave, const RECTL *prcl)
{
    if (prcl->left >= prcl->right || prcl->top >= prcl->bottom) {
        return;
    }
    if (!*phave) {
        *pu = *prcl;
        *phave = 1;
        return;
    }
    if (prcl->left < pu->left) {
        pu->left = prcl->left;
    }
    if (prcl->top < pu->top) {
        pu->top = prcl->top;
    }
    if (prcl->right > pu->right) {
        pu->right = prcl->right;
    }
    if (prcl->bottom > pu->bottom) {
        pu->bottom = prcl->bottom;
    }
}

static int mr_glyph_box(STROBJ *pstro, RECTL *pbox)
{
    GLYPHPOS  *pgp;
    GLYPHBITS *pgb;
    RECTL      r;
    ULONG      c, i;
    BOOL       more;
    LONG       x, y;
    int        have = 0;

    STROBJ_vEnumStart(pstro);
    do {
        if (pstro->pgp != NULL) {
            pgp = pstro->pgp;
            c = pstro->cGlyphs;
            more = FALSE;
        } else {
            more = STROBJ_bEnum(pstro, &c, &pgp);
        }
        for (i = 0; i < c; i++) {
            if (pgp[i].pgdf == NULL || pgp[i].pgdf->pgb == NULL) {
                STROBJ_vEnumStart(pstro);
                return -1;
            }
            pgb = pgp[i].pgdf->pgb;
            if (pstro->ulCharInc != 0) {
                x = pgp[0].ptl.x + (LONG) (i * pstro->ulCharInc);
                y = pgp[0].ptl.y;
            } else {
                x = pgp[i].ptl.x;
                y = pgp[i].ptl.y;
            }
            r.left = x + (pgb->ptlOrigin.x < 0 ? pgb->ptlOrigin.x : 0);
            r.right = x + (pgb->ptlOrigin.x > 0 ? pgb->ptlOrigin.x : 0) +
                      (pgb->sizlBitmap.cx > 1 ? pgb->sizlBitmap.cx : 1);
            r.top = y + pgb->ptlOrigin.y;
            r.bottom = r.top +
                       (pgb->sizlBitmap.cy > 1 ? pgb->sizlBitmap.cy : 1);
            mr_grow(pbox, &have, &r);
        }
    } while (more);
    STROBJ_vEnumStart(pstro);
    return have;
}

BOOL APIENTRY mrTextOut(SURFOBJ *pso, STROBJ *pstro, FONTOBJ *pfo,
    CLIPOBJ *pco, RECTL *prclExtra, RECTL *prclOpaque, BRUSHOBJ *pboFore,
    BRUSHOBJ *pboOpaque, POINTL *pptlOrg, MIX mix)
{
    MR_PDEV *ppdev = mr_target(pso);
    RECTL    u, g, bk, tb, opq;
    RECTL   *popq = NULL;
    int      have = 0, havetb = 0, glyphs;
    ULONG    i, why = MR_RW_OTHER;
    BOOL     ok, done = FALSE;

    if (ppdev == NULL) {
        return TRUE;
    }
    if (pstro != NULL) {
        mr_grow(&u, &have, &pstro->rclBkGround);
    }
    if (prclOpaque != NULL) {
        opq = *prclOpaque;
        mr_grow(&u, &have, &opq);
        if (opq.left < opq.right && opq.top < opq.bottom) {
            popq = &opq;
        }
    }
    if (prclExtra != NULL) {
        for (i = 0; i < MR_MAXEXTRA &&
                    prclExtra[i].left != prclExtra[i].right; i++) {
            mr_grow(&u, &have, &prclExtra[i]);
        }
    }
    if (ppdev->psoShadow != NULL) {
        glyphs = -1;
        if (pstro != NULL && pfo != NULL) {
            glyphs = mr_glyph_box(pstro, &g);
        }
        if (glyphs < 0) {
            g_skipped++;
        } else {
            bk = pstro->rclBkGround;
            mr_grow(&tb, &havetb, &bk);
            if (glyphs > 0) {
#ifndef MR_LAYER
                mr_grow(&tb, &havetb, &g);
#endif
                mr_grow(&u, &have, &g);
            }
            if (!havetb) {
                if (popq != NULL || (prclExtra != NULL &&
                    prclExtra[0].left != prclExtra[0].right)) {
                    g_skipped++;
                }
            } else if (mr_inside(ppdev, pco, &u)) {
                pstro->rclBkGround = tb;
                ok = EngTextOut(ppdev->psoShadow, pstro, pfo, pco, prclExtra,
                                popq, pboFore, pboOpaque, pptlOrg, mix);
#ifdef MR_LAYER
                mr_glyphs_back(pstro);
#endif
                if (ok && g_text && glyphs > 0 && mr_ord_on(ppdev)) {
                    done = mr_text_order(ppdev, pstro, pfo, pco, prclExtra,
                                         popq, pboFore, pboOpaque, &tb);
                }
                pstro->rclBkGround = bk;
                if (!ok) {
                    g_skipped++;
                } else {
                    why = g_text && mr_ord_on(ppdev) ? MR_RW_TEXT
                                                     : MR_RW_OFF;
                }
            } else {
                g_skipped++;
            }
        }
    }
    if (done) {
        return TRUE;
    }
    if (have) {
        mr_rcl(ppdev, pco, &u, why);
    } else {
        mr_all(ppdev, pco, why);
    }
    return TRUE;
}

BOOL APIENTRY mrStrokePath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco,
    XFORMOBJ *pxo, BRUSHOBJ *pbo, POINTL *pptlBrushOrg,
    LINEATTRS *plineattrs, MIX mix)
{
    MR_PDEV *ppdev = mr_target(pso);
    RECTL    r;

    if (ppdev == NULL) {
        return TRUE;
    }
    if (ppdev->psoShadow != NULL) {
        if (ppo != NULL && plineattrs != NULL) {
            mr_path_rect(ppo, &r);
        }
        if (ppo != NULL && plineattrs != NULL && mr_inside(ppdev, pco, &r)) {
            if (!EngStrokePath(ppdev->psoShadow, ppo, pco, pxo, pbo,
                               pptlBrushOrg, plineattrs, mix)) {
                g_skipped++;
            }
        } else {
            g_skipped++;
        }
    }
    mr_stroke(ppdev, pco, ppo, plineattrs);
    return TRUE;
}

BOOL APIENTRY mrFillPath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco,
    BRUSHOBJ *pbo, POINTL *pptlBrushOrg, MIX mix, FLONG flOptions)
{
    MR_PDEV *ppdev = mr_target(pso);
    RECTL    r;

    if (ppdev == NULL) {
        return TRUE;
    }
    if (ppdev->psoShadow != NULL) {
        if (ppo != NULL) {
            mr_path_rect(ppo, &r);
        }
        if (ppo != NULL && mr_inside(ppdev, pco, &r)) {
            if (!EngFillPath(ppdev->psoShadow, ppo, pco, pbo, pptlBrushOrg,
                             mix, flOptions)) {
                g_skipped++;
            }
        } else {
            g_skipped++;
        }
    }
    mr_path(ppdev, pco, ppo, 0);
    return TRUE;
}

BOOL APIENTRY mrStrokeAndFillPath(SURFOBJ *pso, PATHOBJ *ppo,
    CLIPOBJ *pco, XFORMOBJ *pxo, BRUSHOBJ *pboStroke, LINEATTRS *plineattrs,
    BRUSHOBJ *pboFill, POINTL *pptlBrushOrg, MIX mixFill, FLONG flOptions)
{
    MR_PDEV *ppdev = mr_target(pso);
    RECTL    r;

    if (ppdev == NULL) {
        return TRUE;
    }
    if (ppdev->psoShadow != NULL) {
        if (ppo != NULL && plineattrs != NULL) {
            mr_path_rect(ppo, &r);
        }
        if (ppo != NULL && plineattrs != NULL && mr_inside(ppdev, pco, &r)) {
            if (!EngStrokeAndFillPath(ppdev->psoShadow, ppo, pco, pxo,
                                      pboStroke, plineattrs, pboFill,
                                      pptlBrushOrg, mixFill, flOptions)) {
                g_skipped++;
            }
        } else {
            g_skipped++;
        }
    }
    mr_stroke(ppdev, pco, ppo, plineattrs);
    return TRUE;
}

BOOL APIENTRY mrPaint(SURFOBJ *pso, CLIPOBJ *pco, BRUSHOBJ *pbo,
    POINTL *pptlBrushOrg, MIX mix)
{
    MR_PDEV *ppdev = mr_target(pso);
    RECTL    r;
    ULONG    r2, why = MR_RW_OTHER;

    if (ppdev == NULL) {
        return TRUE;
    }
    if (ppdev->psoShadow != NULL) {
        if (pco != NULL && mr_inside(ppdev, pco, NULL)) {
            if (!EngPaint(ppdev->psoShadow, pco, pbo, pptlBrushOrg, mix)) {
                g_skipped++;
            } else if (!mr_ord_on(ppdev)) {
                why = MR_RW_OFF;
            } else if ((r2 = mr_mix(mix)) != 0) {
                r.left = 0;
                r.top = 0;
                r.right = ppdev->cx;
                r.bottom = ppdev->cy;
                why = mr_fill_order(ppdev, pco, &r, pbo, pptlBrushOrg,
                                    mr_rop3(r2), MR_O_FILL, 0);
                if (why == MR_DONE) {
                    return TRUE;
                }
            }
        } else {
            g_skipped++;
        }
    }
    mr_all(ppdev, pco, why);
    return TRUE;
}

static ULONG mr_pixel(const BYTE *row, ULONG fmt, LONG x)
{
    const BYTE *pb;

    switch (fmt) {
    case BMF_1BPP:
        return (row[x >> 3] >> (7 - (x & 7))) & 1;
    case BMF_4BPP:
        return (x & 1) ? (row[x >> 1] & 0x0F) : (row[x >> 1] >> 4);
    case BMF_8BPP:
        return row[x];
    case BMF_16BPP:
        pb = row + 2 * x;
        return pb[0] | ((ULONG) pb[1] << 8);
    case BMF_24BPP:
        pb = row + 3 * x;
        return pb[0] | ((ULONG) pb[1] << 8) | ((ULONG) pb[2] << 16);
    case BMF_32BPP:
        pb = row + 4 * x;
        return pb[0] | ((ULONG) pb[1] << 8) | ((ULONG) pb[2] << 16) |
               ((ULONG) pb[3] << 24);
    }
    return 0;
}

static void mr_copy_shape(SURFOBJ *psoMask, SURFOBJ *psoColor,
    XLATEOBJ *pxlo, LONG xHot, LONG yHot, FLONG fl)
{
    MR_PTROUT *ps = &g_shape;
    const BYTE *row;
    BYTE *dst;
    ULONG fmt, v;
    LONG cx, cy, w, h, x, y, nb;
    BOOL xl;

    mr_zero(ps, sizeof(MR_PTROUT));
    ps->serial = g_ptr.shape;
    ps->flags = MR_P_SHAPE;
    ps->fl = fl;
    ps->hotx = xHot;
    ps->hoty = yHot;
    ps->xlate = pxlo != NULL ? pxlo->flXlate : 0xFFFFFFFFUL;
    if (psoMask == NULL) {
        return;
    }
    cx = psoMask->sizlBitmap.cx;
    cy = psoMask->sizlBitmap.cy / 2;
    ps->cx = cx;
    ps->cy = cy;
    w = cx < MR_PTR_MAX ? cx : MR_PTR_MAX;
    h = cy < MR_PTR_MAX ? cy : MR_PTR_MAX;
    if (w < cx || h < cy) {
        ps->flags |= MR_P_CUT;
    }
    if (w <= 0 || h <= 0 || psoMask->iBitmapFormat != BMF_1BPP ||
        psoMask->pvScan0 == NULL) {
        ps->flags |= MR_P_NOBITS;
        return;
    }
    nb = (w + 7) / 8;
    for (y = 0; y < h; y++) {
        row = (const BYTE *) psoMask->pvScan0 + y * psoMask->lDelta;
        dst = (BYTE *) &ps->and_mask[y];
        for (x = 0; x < nb; x++) {
            dst[x] = row[x];
        }
        row = (const BYTE *) psoMask->pvScan0 + (cy + y) * psoMask->lDelta;
        dst = (BYTE *) &ps->xor_mask[y];
        for (x = 0; x < nb; x++) {
            dst[x] = row[x];
        }
    }
    if (psoColor == NULL) {
        return;
    }
    ps->flags |= MR_P_COLOR;
    fmt = psoColor->iBitmapFormat;
    ps->format = fmt;
    if (fmt < BMF_1BPP || fmt > BMF_32BPP || psoColor->pvScan0 == NULL ||
        psoColor->sizlBitmap.cx < w || psoColor->sizlBitmap.cy < h) {
        ps->flags |= MR_P_NOBITS;
        return;
    }
    xl = pxlo != NULL && (pxlo->flXlate & XO_TRIVIAL) == 0;
    for (y = 0; y < h; y++) {
        row = (const BYTE *) psoColor->pvScan0 + y * psoColor->lDelta;
        for (x = 0; x < w; x++) {
            v = mr_pixel(row, fmt, x);
            ps->pix[y][x] = xl ? XLATEOBJ_iXlate(pxlo, v) : v;
        }
    }
}

static void mr_ptr_changed(void)
{
    g_ptr.changed = 1;
    mr_signal();
}

VOID mr_ptr_shape(SURFOBJ *psoMask, SURFOBJ *psoColor, XLATEOBJ *pxlo,
                  LONG xHot, LONG yHot, LONG x, LONG y, FLONG fl)
{
    g_ptr.shape++;
    g_ptr.flags = fl;
    g_ptr.hotx = xHot;
    g_ptr.hoty = yHot;
    g_ptr.cx = psoMask != NULL ? psoMask->sizlBitmap.cx : 0;
    g_ptr.cy = psoMask != NULL ? psoMask->sizlBitmap.cy : 0;
    g_ptr.color = psoColor != NULL ? psoColor->iBitmapFormat : 0;
    g_ptr.hidden = x == -1;
    g_ptr.posx = x;
    g_ptr.posy = y;
    mr_copy_shape(psoMask, psoColor, pxlo, xHot, yHot, fl);
    mr_ptr_changed();
}

VOID mr_ptr_move(LONG x, LONG y)
{
    ULONG hidden = x == -1;

    g_ptr.move++;
    g_ptr.posx = x;
    g_ptr.posy = y;
    if (hidden != g_ptr.hidden) {
        g_ptr.hidden = hidden;
        mr_ptr_changed();
    }
}

static ULONG mr_pointer_out(ULONG cjOut, PVOID pvOut)
{
    MR_PTROUT *po = (MR_PTROUT *) pvOut;
    const BYTE *src = (const BYTE *) &g_shape;
    BYTE *dst = (BYTE *) pvOut;
    ULONG i;

    if (cjOut < sizeof(MR_PTROUT)) {
        return 0;
    }
    for (i = 0; i < sizeof(MR_PTROUT); i++) {
        dst[i] = src[i];
    }
    po->magic = MR_MAGIC;
    po->version = MR_VERSION;
    po->status = MR_OK;
    if (g_ptr.hidden) {
        po->flags |= MR_P_HIDDEN;
    }
    po->posx = g_ptr.posx;
    po->posy = g_ptr.posy;
    return 1;
}

ULONG mr_escape(MR_PDEV *ppdev, HDEV hdev, ULONG diag, MR_IN *pin,
                ULONG cjOut, PVOID pvOut)
{
    MR_OUT      *pout;
    MR_FLUSHOUT *pf;

    if (pin->magic == MR_MAGIC && pin->version == MR_VERSION &&
        pin->cmd == MR_CMD_POINTER) {
        return mr_pointer_out(cjOut, pvOut);
    }
    pout = (MR_OUT *) pvOut;
    mr_zero(pout, cjOut >= sizeof(MR_FLUSHOUT) ? sizeof(MR_FLUSHOUT)
                                                : sizeof(MR_OUT));
    pout->magic = MR_MAGIC;
    pout->version = MR_VERSION;
    pout->engine = g_engine;
    if (ppdev == NULL) {
        pout->status = MR_STOPPED;
        return 1;
    }
    if (hdev == NULL) {
        hdev = ppdev->hdev;
    }

    if (pin->magic != MR_MAGIC || pin->version != MR_VERSION) {
        pout->status = MR_BADCMD;
    } else {
        switch (pin->cmd) {
        case MR_CMD_START:
            pout->status = mr_chan_start(ppdev, hdev, pin->arg);
            break;
        case MR_CMD_GET:
            pout->status = mr_get(ppdev, pout);
            if (pout->status == MR_OK && g_ptr.changed) {
                g_ptr.changed = 0;
                pout->flags |= MR_F_POINTER;
            }
            break;
        case MR_CMD_FLUSH:
            if (cjOut < MR_FLUSH_MIN) {
                pout->status = MR_BADCMD;
                break;
            }
            pf = (MR_FLUSHOUT *) pvOut;
            pf->skipped = g_skipped;
            pf->saves = g_saves;
            pf->savefail = g_savefail;
            pout->status = mr_flush(ppdev, pf, cjOut, pin->arg);
            if (pout->status == MR_OK && g_ptr.changed) {
                g_ptr.changed = 0;
                pout->flags |= MR_F_POINTER;
            }
            break;
        case MR_CMD_STOP:
            pout->status = mr_chan_stop();
            break;
        default:
            pout->status = MR_BADCMD;
            break;
        }
    }
    if (ppdev->fullscreen) {
        pout->flags |= MR_F_FULLSCREEN;
    }
    pout->cx = ppdev->cx;
    pout->cy = ppdev->cy;
    pout->bpp = ppdev->bpp;
    pout->diag = diag | ppdev->diag;
    pout->calls = ppdev->rec.calls;
    pout->reads = g_reads;
    pout->ptrshape = g_ptr.shape;
    pout->ptrmove = g_ptr.move;
    pout->ptrflags = g_ptr.flags;
    pout->ptrcolor = g_ptr.color;
    pout->ptrcx = g_ptr.cx;
    pout->ptrcy = g_ptr.cy;
    pout->ptrhotx = g_ptr.hotx;
    pout->ptrhoty = g_ptr.hoty;
    return 1;
}
