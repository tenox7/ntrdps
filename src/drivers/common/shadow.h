/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_SHADOW_H
#define NTRDPS_SHADOW_H

#include "mrchan.h"
#include "record.h"

#define MR_MAXPDEV      4
#define MR_MAXEXTRA     16
#define MR_GHASH        2048
#define MR_BHASH        2048
#define MR_PIECES       16
#define MR_PAT_SIZE     64
#define MR_SHADOW_MAX   0x02000000UL

#define MR_DONE 0xFFFFFFFFUL

typedef struct MR_SAVE {
    HSURF    hsurf;
    SURFOBJ *pso;
    RECTL    rcl;
    ULONG    num;
    ULONG    ord;
    MR_RECT  stale;
    ULONG    realid;
} MR_SAVE;

#define MR_SV_NONE      0
#define MR_SV_QUEUED    1
#define MR_SV_SENT      2

typedef struct MR_PDEV {
    HDEV     hdev;
    HSURF    hsurf;
    HPALETTE hpalOwn;
    DHPDEV   dhpdevReal;
    LONG     cx;
    LONG     cy;
    ULONG    bpp;
    ULONG    iFormat;
    ULONG    diag;
    BOOL     fullscreen;
    REC_LIST rec;
    PVOID    shadowBits;
    HSURF    hsurfShadow;
    SURFOBJ *psoShadow;
    MR_SAVE  save[MR_SAVES];
    ULONG    nord;
    MR_ORDER ord[MR_ORDERS];
    ULONG       nplace;
    ULONG       glyphlen;
    MR_GLYPHPOS place[MR_PLACES];
    ULONG       ghash[MR_GHASH];
    BYTE        glyphs[MR_GLYPHAREA];
    ULONG       nbmp;
    ULONG       bmplen;
    ULONG       bhash[MR_BHASH];
    BYTE        bitmaps[MR_BITMAPAREA];
    PVOID       scratchBits;
    HSURF       hsurfScratch;
    SURFOBJ    *psoScratch;
    PVOID       patBits;
    HSURF       hsurfPat;
    SURFOBJ    *psoPat;
#ifdef NTRDPS_FRAMEBUF
    HSURF       hsurfBmp;
    FLONG       flReal;
#endif
} MR_PDEV;

typedef struct MR_PTR {
    ULONG shape;
    ULONG move;
    ULONG flags;
    ULONG color;
    LONG  cx;
    LONG  cy;
    LONG  hotx;
    LONG  hoty;
    ULONG hidden;
    ULONG changed;
    LONG  posx;
    LONG  posy;
} MR_PTR;

PVOID    mr_alloc(ULONG cb);
VOID     mr_free(PVOID pv);
VOID     mr_signal(VOID);
MR_PDEV *mr_target(SURFOBJ *pso);
ULONG    mr_chan_start(MR_PDEV *ppdev, HDEV hdev, ULONG arg);
ULONG    mr_chan_stop(VOID);
ULONG    mr_chan_check(VOID);
#ifdef MR_LAYER
VOID     mr_glyphs_back(STROBJ *pstro);
#endif

extern MR_PDEV  *g_pdev[MR_MAXPDEV];
extern MR_PDEV  *g_cur;
extern ULONG     g_engine;
extern ULONG     g_reads;
extern MR_PTR    g_ptr;
extern ULONG     g_skipped;
extern ULONG     g_saves;
extern ULONG     g_savefail;

VOID     mr_zero(PVOID pv, ULONG cb);
VOID     mr_copy(PVOID dst, const VOID *src, ULONG cb);
MR_PDEV *mr_find(PVOID pv);

VOID     mr_rect(MR_PDEV *ppdev, CLIPOBJ *pco, LONG l, LONG t, LONG r,
                 LONG b, ULONG why);
VOID     mr_rcl(MR_PDEV *ppdev, CLIPOBJ *pco, RECTL *prcl, ULONG why);
VOID     mr_all(MR_PDEV *ppdev, CLIPOBJ *pco, ULONG why);

VOID     mr_shadow_make(MR_PDEV *ppdev);
VOID     mr_shadow_free(MR_PDEV *ppdev);
BOOL     mr_inside(MR_PDEV *ppdev, CLIPOBJ *pco, const RECTL *prcl);

BOOL     mr_ord_on(MR_PDEV *ppdev);
ULONG    mr_rop3(ULONG r2);
ULONG    mr_mix(MIX mix);
ULONG    mr_fill_order(MR_PDEV *ppdev, CLIPOBJ *pco, const RECTL *prcl,
                       BRUSHOBJ *pbo, const POINTL *pptlBrush, ULONG rop,
                       ULONG kind, ULONG r2);

ULONG    mr_save(MR_PDEV *ppdev, const RECTL *prcl);
VOID     mr_save_order(MR_PDEV *ppdev, MR_SAVE *ps);
BOOL     mr_restore(MR_PDEV *ppdev, ULONG ident, const RECTL *prcl);
BOOL     mr_restore_area(MR_PDEV *ppdev, ULONG ident, const RECTL *prcl,
                         RECTL *prd);
VOID     mr_save_free(MR_SAVE *ps);

VOID     mr_start_reset(MR_PDEV *ppdev);
VOID     mr_stop_reset(VOID);
ULONG    mr_escape(MR_PDEV *ppdev, HDEV hdev, ULONG diag, MR_IN *pin,
                   ULONG cjOut, PVOID pvOut);

VOID     mr_ptr_shape(SURFOBJ *psoMask, SURFOBJ *psoColor, XLATEOBJ *pxlo,
                      LONG xHot, LONG yHot, LONG x, LONG y, FLONG fl);
VOID     mr_ptr_move(LONG x, LONG y);

BOOL APIENTRY mrBitBlt(SURFOBJ *psoTrg, SURFOBJ *psoSrc, SURFOBJ *psoMask,
    CLIPOBJ *pco, XLATEOBJ *pxlo, RECTL *prclTrg, POINTL *pptlSrc,
    POINTL *pptlMask, BRUSHOBJ *pbo, POINTL *pptlBrush, ROP4 rop4);
BOOL APIENTRY mrCopyBits(SURFOBJ *psoDest, SURFOBJ *psoSrc, CLIPOBJ *pco,
    XLATEOBJ *pxlo, RECTL *prclDest, POINTL *pptlSrc);
BOOL APIENTRY mrStretchBlt(SURFOBJ *psoDest, SURFOBJ *psoSrc,
    SURFOBJ *psoMask, CLIPOBJ *pco, XLATEOBJ *pxlo, COLORADJUSTMENT *pca,
    POINTL *pptlHTOrg, RECTL *prclDest, RECTL *prclSrc, POINTL *pptlMask,
    ULONG iMode);
BOOL APIENTRY mrTextOut(SURFOBJ *pso, STROBJ *pstro, FONTOBJ *pfo,
    CLIPOBJ *pco, RECTL *prclExtra, RECTL *prclOpaque, BRUSHOBJ *pboFore,
    BRUSHOBJ *pboOpaque, POINTL *pptlOrg, MIX mix);
BOOL APIENTRY mrStrokePath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco,
    XFORMOBJ *pxo, BRUSHOBJ *pbo, POINTL *pptlBrushOrg,
    LINEATTRS *plineattrs, MIX mix);
BOOL APIENTRY mrFillPath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco,
    BRUSHOBJ *pbo, POINTL *pptlBrushOrg, MIX mix, FLONG flOptions);
BOOL APIENTRY mrStrokeAndFillPath(SURFOBJ *pso, PATHOBJ *ppo,
    CLIPOBJ *pco, XFORMOBJ *pxo, BRUSHOBJ *pboStroke, LINEATTRS *plineattrs,
    BRUSHOBJ *pboFill, POINTL *pptlBrushOrg, MIX mixFill, FLONG flOptions);
BOOL APIENTRY mrPaint(SURFOBJ *pso, CLIPOBJ *pco, BRUSHOBJ *pbo,
    POINTL *pptlBrushOrg, MIX mix);

#endif
