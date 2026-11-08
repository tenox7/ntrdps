/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <stddef.h>
#include <stdarg.h>
#include <windef.h>
#include <wingdi.h>
#include <winddi.h>

#include "sp3ddi.h"
#include "shadow.h"

#define MR_DDI_RTM      0x00020000L
#define MR_DDI_SP3      0x00020003L
#define MR_DDI_NT5      0x00030000L

#define MR_TAG          0x724D524EL

#define MR_HOOKS (HOOK_BITBLT | HOOK_STRETCHBLT | HOOK_TEXTOUT | \
                  HOOK_PAINT | HOOK_STROKEPATH | HOOK_FILLPATH | \
                  HOOK_STROKEANDFILLPATH | HOOK_LINETO | HOOK_COPYBITS | \
                  HOOK_SYNCHRONIZEACCESS)

static ULONG            g_pal[256];

static HDRVOBJ          g_hdo;
#ifndef MR_RTM
static PEVENT volatile  g_event;
static volatile LONG    g_inuse;
#endif

#ifndef MR_RTM
#ifdef _X86_
static void mr_inuse_inc(void)
{
    __asm lock inc dword ptr g_inuse
}

static void mr_inuse_dec(void)
{
    __asm lock dec dword ptr g_inuse
}

static void mr_event_clear(void)
{
    __asm {
        xor eax, eax
        xchg eax, g_event
    }
}
#else
static HSEMAPHORE       g_sem;

static void mr_inuse_inc(void)
{
    EngAcquireSemaphore(g_sem);
    g_inuse++;
    EngReleaseSemaphore(g_sem);
}

static void mr_inuse_dec(void)
{
    EngAcquireSemaphore(g_sem);
    g_inuse--;
    EngReleaseSemaphore(g_sem);
}

static void mr_event_clear(void)
{
    EngAcquireSemaphore(g_sem);
    g_event = NULL;
    EngReleaseSemaphore(g_sem);
}
#endif
#endif

static void mr_std_palette(ULONG bpp)
{
    static const BYTE res[20][3] = {
        { 0x00, 0x00, 0x00 }, { 0x80, 0x00, 0x00 }, { 0x00, 0x80, 0x00 },
        { 0x80, 0x80, 0x00 }, { 0x00, 0x00, 0x80 }, { 0x80, 0x00, 0x80 },
        { 0x00, 0x80, 0x80 }, { 0xC0, 0xC0, 0xC0 }, { 0xC0, 0xDC, 0xC0 },
        { 0xA6, 0xCA, 0xF0 }, { 0xFF, 0xFB, 0xF0 }, { 0xA0, 0xA0, 0xA4 },
        { 0x80, 0x80, 0x80 }, { 0xFF, 0x00, 0x00 }, { 0x00, 0xFF, 0x00 },
        { 0xFF, 0xFF, 0x00 }, { 0x00, 0x00, 0xFF }, { 0xFF, 0x00, 0xFF },
        { 0x00, 0xFF, 0xFF }, { 0xFF, 0xFF, 0xFF }
    };
    static const BYTE vga[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 12, 13, 14, 15, 16, 17, 18, 19
    };
    ULONG i, k;

    if (bpp == 4) {
        for (i = 0; i < 16; i++) {
            k = vga[i];
            g_pal[i] = (ULONG) res[k][0] | ((ULONG) res[k][1] << 8) |
                       ((ULONG) res[k][2] << 16);
        }
        return;
    }
    for (i = 0; i < 256; i++) {
        g_pal[i] = ((i & 7) << 5) | (((i >> 3) & 7) << 13) |
                   ((i >> 6) << 22);
    }
    for (i = 0; i < 10; i++) {
        g_pal[i] = (ULONG) res[i][0] | ((ULONG) res[i][1] << 8) |
                   ((ULONG) res[i][2] << 16);
        k = i + 10;
        g_pal[246 + i] = (ULONG) res[k][0] | ((ULONG) res[k][1] << 8) |
                         ((ULONG) res[k][2] << 16);
    }
}

PVOID mr_alloc(ULONG cb)
{
    return EngAllocMem(FL_ZERO_MEMORY, cb, MR_TAG);
}

VOID mr_free(PVOID pv)
{
    EngFreeMem(pv);
}

MR_PDEV *mr_target(SURFOBJ *pso)
{
    if (pso == NULL || pso->iType != STYPE_DEVICE) {
        return NULL;
    }
    return mr_find((PVOID) pso->dhsurf);
}

#ifdef MR_RTM

VOID mr_signal(VOID)
{
}

static BOOL CALLBACK mr_gone(DRIVEROBJ *pdo)
{
    g_hdo = NULL;
    return TRUE;
}

static BOOL mr_owner(void)
{
    HDRVOBJ hdo = g_hdo;

    if (hdo == NULL || EngLockDriverObj(hdo) == NULL) {
        return FALSE;
    }
    EngUnlockDriverObj(hdo);
    return TRUE;
}

ULONG mr_chan_stop(VOID)
{
    HDRVOBJ hdo = g_hdo;

    mr_stop_reset();
    if (hdo == NULL) {
        return MR_OK;
    }
    if (EngLockDriverObj(hdo) == NULL) {
        return MR_BUSY;
    }
    if (!EngDeleteDriverObj(hdo, TRUE, TRUE)) {
        EngUnlockDriverObj(hdo);
        if (g_hdo != NULL) {
            return MR_BUSY;
        }
    }
    return MR_OK;
}

ULONG mr_chan_start(MR_PDEV *ppdev, HDEV hdev, ULONG arg)
{
    HDRVOBJ hdo;
    ULONG   st;

    if (g_hdo != NULL) {
        st = mr_chan_stop();
        if (st != MR_OK) {
            return st;
        }
    }
    if (hdev == NULL) {
        return MR_BADEVENT;
    }
    hdo = EngCreateDriverObj(NULL, mr_gone, hdev);
    if (hdo == NULL) {
        return MR_BADEVENT;
    }
    g_hdo = hdo;
    mr_start_reset(ppdev);
    return MR_OK;
}

ULONG mr_chan_check(VOID)
{
    if (g_hdo == NULL) {
        return MR_STOPPED;
    }
    return mr_owner() ? MR_OK : MR_BUSY;
}

#else

VOID mr_signal(VOID)
{
    PEVENT pe;

    mr_inuse_inc();
    pe = g_event;
    if (pe != NULL) {
        EngSetEvent(pe);
    }
    mr_inuse_dec();
}

static BOOL APIENTRY mr_gone(PVOID pv)
{
    mr_event_clear();
    g_hdo = NULL;
    while (g_inuse != 0) {
    }
    return TRUE;
}

static BOOL mr_owner(void)
{
    HDRVOBJ hdo = g_hdo;

    if (hdo == NULL || EngLockDriverObj(hdo) == NULL) {
        return FALSE;
    }
    EngUnlockDriverObj(hdo);
    return TRUE;
}

ULONG mr_chan_stop(VOID)
{
    HDRVOBJ hdo = g_hdo;

    mr_stop_reset();
    if (hdo == NULL) {
        return MR_OK;
    }
    if (EngLockDriverObj(hdo) == NULL) {
        return MR_BUSY;
    }
    if (!EngUnmapEvent(hdo)) {
        EngUnlockDriverObj(hdo);
        if (g_hdo != NULL) {
            return MR_BUSY;
        }
    }
    return MR_OK;
}

ULONG mr_chan_start(MR_PDEV *ppdev, HDEV hdev, ULONG arg)
{
    HANDLE  hEvent = (HANDLE) arg;
    PEVENT  pe = NULL;
    HDRVOBJ hdo;
    ULONG   st;

    if (g_hdo != NULL) {
        st = mr_chan_stop();
        if (st != MR_OK) {
            return st;
        }
    }
    if (hEvent == NULL || hdev == NULL) {
        return MR_BADEVENT;
    }
    hdo = EngMapEvent(hdev, hEvent, &pe, mr_gone, NULL);
    if (hdo == NULL || pe == NULL) {
        return MR_BADEVENT;
    }
    g_hdo = hdo;
    g_event = pe;
    mr_start_reset(ppdev);
    return MR_OK;
}

ULONG mr_chan_check(VOID)
{
    if (g_event == NULL) {
        return MR_STOPPED;
    }
    return mr_owner() ? MR_OK : MR_BUSY;
}

#endif

static void mr_chroma(CIECHROMA *pc, LDECI4 x, LDECI4 y)
{
    pc->x = x;
    pc->y = y;
    pc->Y = 0;
}

static void mr_fill_gdiinfo(GDIINFO *pgi, DEVMODEW *pdm, MR_PDEV *ppdev,
                            ULONG bits, ULONG planes,
                            ULONG red, ULONG green, ULONG blue)
{
    ULONG dpi = 96;
    COLORINFO *pci = &pgi->ciDevice;

    if (pdm != NULL && pdm->dmLogPixels != 0) {
        dpi = pdm->dmLogPixels;
    }
    mr_zero(pgi, sizeof(GDIINFO));
    pgi->ulVersion = GDI_DRIVER_VERSION;
    pgi->ulTechnology = DT_RASDISPLAY;
    pgi->ulHorzSize = (ULONG) ppdev->cx * 254 / (dpi * 10);
    pgi->ulVertSize = (ULONG) ppdev->cy * 254 / (dpi * 10);
    pgi->ulHorzRes = ppdev->cx;
    pgi->ulVertRes = ppdev->cy;
    pgi->cBitsPixel = bits;
    pgi->cPlanes = planes;
    switch (ppdev->bpp) {
    case 4:
        pgi->ulNumColors = 16;
        pgi->ulHTOutputFormat = HT_FORMAT_4BPP;
        break;
    case 8:
        pgi->ulNumColors = 20;
        pgi->ulNumPalReg = 256;
        pgi->ulHTOutputFormat = HT_FORMAT_8BPP;
        break;
    case 16:
        pgi->ulNumColors = (ULONG) -1;
        pgi->ulHTOutputFormat = HT_FORMAT_16BPP;
        break;
    case 24:
        pgi->ulNumColors = (ULONG) -1;
        pgi->ulHTOutputFormat = HT_FORMAT_24BPP;
        break;
    default:
        pgi->ulNumColors = (ULONG) -1;
        pgi->ulHTOutputFormat = HT_FORMAT_32BPP;
        break;
    }
    pgi->ulLogPixelsX = dpi;
    pgi->ulLogPixelsY = dpi;
    pgi->flTextCaps = TC_RA_ABLE;
    pgi->ulDACRed = red;
    pgi->ulDACGreen = green;
    pgi->ulDACBlue = blue;
    pgi->ulAspectX = 0x24;
    pgi->ulAspectY = 0x24;
    pgi->ulAspectXY = 0x33;
    pgi->xStyleStep = 1;
    pgi->yStyleStep = 1;
    pgi->denStyleStep = 3;
    mr_chroma(&pci->Red, 6700, 3300);
    mr_chroma(&pci->Green, 2100, 7100);
    mr_chroma(&pci->Blue, 1400, 800);
    mr_chroma(&pci->Cyan, 1750, 3950);
    mr_chroma(&pci->Magenta, 4050, 2050);
    mr_chroma(&pci->Yellow, 4400, 5200);
    mr_chroma(&pci->AlignmentWhite, 3127, 3290);
    pci->RedGamma = 20000;
    pci->GreenGamma = 20000;
    pci->BlueGamma = 20000;
    pgi->ulPrimaryOrder = PRIMARY_ORDER_CBA;
    pgi->ulHTPatternSize = HT_PATSIZE_4x4_M;
    pgi->flHTFlags = HT_FLAG_ADDITIVE_PRIMS;
    if (pdm != NULL) {
        pgi->ulVRefresh = pdm->dmDisplayFrequency;
    }
}

static void mr_font(LOGFONTW *plf, LONG height, LONG width, LONG weight,
                    BYTE clip, BYTE quality, BYTE pitch, const WCHAR *face)
{
    ULONG i;

    plf->lfHeight = height;
    plf->lfWidth = width;
    plf->lfWeight = weight;
    plf->lfCharSet = ANSI_CHARSET;
    plf->lfOutPrecision = OUT_DEFAULT_PRECIS;
    plf->lfClipPrecision = clip;
    plf->lfQuality = quality;
    plf->lfPitchAndFamily = pitch;
    for (i = 0; i < LF_FACESIZE - 1 && face[i] != 0; i++) {
        plf->lfFaceName[i] = face[i];
    }
    plf->lfFaceName[i] = 0;
}

static void mr_fill_devinfo(DEVINFO *pdi, MR_PDEV *ppdev, HPALETTE hpal)
{
    mr_zero(pdi, sizeof(DEVINFO));
    pdi->flGraphicsCaps = GCAPS_LAYERED | GCAPS_OPAQUERECT;
    if (ppdev->bpp == 8) {
        pdi->flGraphicsCaps |= GCAPS_PALMANAGED;
    }
    mr_font(&pdi->lfDefaultFont, 16, 7, FW_BOLD, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, (BYTE) (VARIABLE_PITCH | FF_DONTCARE),
            L"System");
    mr_font(&pdi->lfAnsiVarFont, 12, 9, FW_NORMAL, CLIP_STROKE_PRECIS,
            PROOF_QUALITY, (BYTE) (VARIABLE_PITCH | FF_DONTCARE),
            L"MS Sans Serif");
    mr_font(&pdi->lfAnsiFixFont, 12, 9, FW_NORMAL, CLIP_STROKE_PRECIS,
            PROOF_QUALITY, (BYTE) (FIXED_PITCH | FF_DONTCARE),
            L"Courier");
    pdi->cFonts = 0;
    pdi->iDitherFormat = ppdev->iFormat;
    pdi->cxDither = 8;
    pdi->cyDither = 8;
    pdi->hpalDefault = hpal;
}

static DHPDEV APIENTRY mrEnablePDEV(DEVMODEW *pdm, LPWSTR pwszLogAddress,
    ULONG cPat, HSURF *phsurfPatterns, ULONG cjCaps, ULONG *pdevcaps,
    ULONG cjDevInfo, DEVINFO *pdi, HDEV hdev, LPWSTR pwszDeviceName,
    HANDLE hDriver)
{
    GDIINFO  *pgi = (GDIINFO *) pdevcaps;
    MR_PDEV  *ppdev;
    HPALETTE hpal, hpalOwn = NULL;
    ULONG    slot, bits, planes, bpp, fmt, red, green, blue, diag = 0;
    LONG     cx, cy;

    if (pgi == NULL || pdi == NULL || cjCaps < sizeof(GDIINFO) ||
        cjDevInfo < sizeof(DEVINFO)) {
        return NULL;
    }
    for (slot = 0; slot < MR_MAXPDEV && g_pdev[slot] != NULL; slot++) {
    }
    if (slot == MR_MAXPDEV) {
        return NULL;
    }

    bits = pgi->cBitsPixel;
    planes = pgi->cPlanes;
    red = pgi->ulDACRed;
    green = pgi->ulDACGreen;
    blue = pgi->ulDACBlue;
    hpal = pdi->hpalDefault;
    cx = (LONG) pgi->ulHorzRes;
    cy = (LONG) pgi->ulVertRes;
    if (planes != 0) {
        diag |= MR_D_GDIINFO;
    } else {
        bits = pdm != NULL ? pdm->dmBitsPerPel : 0;
        planes = 1;
        red = green = blue = 0;
        hpal = NULL;
    }
    if (pdm != NULL && pdm->dmPelsWidth != 0 && pdm->dmPelsHeight != 0) {
        cx = (LONG) pdm->dmPelsWidth;
        cy = (LONG) pdm->dmPelsHeight;
    }
    if (cx <= 0 || cy <= 0 || cx > 32767 || cy > 32767) {
        return NULL;
    }
    bpp = bits * planes;
    if (bpp == 15) {
        bpp = 16;
    }
    switch (bpp) {
    case 4:  fmt = BMF_4BPP;  break;
    case 8:  fmt = BMF_8BPP;  break;
    case 16: fmt = BMF_16BPP; break;
    case 24: fmt = BMF_24BPP; break;
    case 32: fmt = BMF_32BPP; break;
    default: return NULL;
    }

    if (hpal != NULL) {
        diag |= MR_D_PALETTE;
    } else {
        if (bpp <= 8) {
            mr_std_palette(bpp);
            hpalOwn = EngCreatePalette(PAL_INDEXED, bpp == 8 ? 256 : 16,
                                       g_pal, 0, 0, 0);
            if (red == 0) {
                red = green = blue = 8;
            }
        } else {
            if (red == 0 && bits == 15) {
                red = 0x7C00;
                green = 0x03E0;
                blue = 0x001F;
            } else if (red == 0 && bpp == 16) {
                red = 0xF800;
                green = 0x07E0;
                blue = 0x001F;
            } else if (red == 0) {
                red = 0xFF0000;
                green = 0x00FF00;
                blue = 0x0000FF;
            }
            hpalOwn = EngCreatePalette(PAL_BITFIELDS, 0, NULL,
                                       red, green, blue);
        }
        if (hpalOwn == NULL) {
            return NULL;
        }
        hpal = hpalOwn;
        diag |= MR_D_OWNPAL;
    }

    ppdev = (MR_PDEV *) mr_alloc(sizeof(MR_PDEV));
    if (ppdev == NULL) {
        if (hpalOwn != NULL) {
            EngDeletePalette(hpalOwn);
        }
        return NULL;
    }
    ppdev->hdev = hdev;
    ppdev->hpalOwn = hpalOwn;
    ppdev->cx = cx;
    ppdev->cy = cy;
    ppdev->bpp = bpp;
    ppdev->iFormat = fmt;
    ppdev->diag = diag;
    rec_init(&ppdev->rec, cx, cy);

    mr_fill_gdiinfo(pgi, pdm, ppdev, bits, planes, red, green, blue);
    mr_fill_devinfo(pdi, ppdev, hpal);
    g_pdev[slot] = ppdev;
    return (DHPDEV) ppdev;
}

static VOID APIENTRY mrCompletePDEV(DHPDEV dhpdev, HDEV hdev)
{
    MR_PDEV *ppdev = mr_find((PVOID) dhpdev);

    if (ppdev != NULL) {
        ppdev->hdev = hdev;
    }
}

static VOID APIENTRY mrDisablePDEV(DHPDEV dhpdev)
{
    MR_PDEV *ppdev = mr_find((PVOID) dhpdev);
    ULONG i;

    if (ppdev == NULL) {
        return;
    }
    for (i = 0; i < MR_MAXPDEV; i++) {
        if (g_pdev[i] == ppdev) {
            g_pdev[i] = NULL;
        }
    }
    if (g_cur == ppdev) {
        g_cur = NULL;
    }
    mr_shadow_free(ppdev);
    if (ppdev->hpalOwn != NULL) {
        EngDeletePalette(ppdev->hpalOwn);
    }
    mr_free(ppdev);
}

static HSURF APIENTRY mrEnableSurface(DHPDEV dhpdev)
{
    MR_PDEV *ppdev = mr_find((PVOID) dhpdev);
    SIZEL   sizl;
    HSURF   hsurf;

    if (ppdev == NULL) {
        return NULL;
    }
    sizl.cx = ppdev->cx;
    sizl.cy = ppdev->cy;
    hsurf = EngCreateDeviceSurface((DHSURF) ppdev, sizl, ppdev->iFormat);
    if (hsurf == NULL) {
        return NULL;
    }
    if (!EngAssociateSurface(hsurf, ppdev->hdev, MR_HOOKS)) {
        EngDeleteSurface(hsurf);
        return NULL;
    }
    ppdev->hsurf = hsurf;
    mr_shadow_make(ppdev);
    g_cur = ppdev;
    if (rec_flag(&ppdev->rec, MR_F_ALL | MR_F_MODE)) {
        mr_signal();
    }
    return hsurf;
}

static VOID APIENTRY mrDisableSurface(DHPDEV dhpdev)
{
    MR_PDEV *ppdev = mr_find((PVOID) dhpdev);

    if (ppdev == NULL || ppdev->hsurf == NULL) {
        return;
    }
    if (g_cur == ppdev) {
        g_cur = NULL;
    }
    mr_shadow_free(ppdev);
    EngDeleteSurface(ppdev->hsurf);
    ppdev->hsurf = NULL;
}

static BOOL APIENTRY mrAssertMode(DHPDEV dhpdev, BOOL bEnable)
{
    MR_PDEV *ppdev = mr_find((PVOID) dhpdev);

    if (ppdev != NULL) {
        ppdev->fullscreen = !bEnable;
        if (rec_flag(&ppdev->rec,
                     bEnable ? (MR_F_ALL | MR_F_MODE) : MR_F_MODE)) {
            mr_signal();
        }
    }
    return TRUE;
}

static ULONG APIENTRY mrGetModes(HANDLE hDriver, ULONG cjSize, DEVMODEW *pdm)
{
    return 0;
}

static BOOL APIENTRY mrSetPalette(DHPDEV dhpdev, PALOBJ *ppalo, FLONG fl,
                                  ULONG iStart, ULONG cColors)
{
    MR_PDEV *ppdev = mr_find((PVOID) dhpdev);

    if (ppdev != NULL && rec_flag(&ppdev->rec, MR_F_PALETTE)) {
        mr_signal();
    }
    return TRUE;
}

static ULONG mr_line_order(MR_PDEV *ppdev, CLIPOBJ *pco, BRUSHOBJ *pbo,
    LONG x1, LONG y1, LONG x2, LONG y2, MIX mix)
{
    ULONG r2 = mr_mix(mix);
    RECTL r;

    if (r2 == 0) {
        return MR_RW_OTHER;
    }
    if (y1 == y2 && x1 != x2) {
        r.top = y1;
        r.bottom = y1 + 1;
        r.left = x1 < x2 ? x1 : x2 + 1;
        r.right = x1 < x2 ? x2 : x1 + 1;
    } else if (x1 == x2 && y1 != y2) {
        r.left = x1;
        r.right = x1 + 1;
        r.top = y1 < y2 ? y1 : y2 + 1;
        r.bottom = y1 < y2 ? y2 : y1 + 1;
    } else {
        return MR_RW_SHAPE;
    }
    return mr_fill_order(ppdev, pco, &r, pbo, NULL, mr_rop3(r2), MR_O_LINE,
                         r2);
}

static BOOL APIENTRY mrLineTo(SURFOBJ *pso, CLIPOBJ *pco, BRUSHOBJ *pbo,
    LONG x1, LONG y1, LONG x2, LONG y2, RECTL *prclBounds, MIX mix)
{
    MR_PDEV *ppdev = mr_target(pso);
    RECTL    r;
    ULONG    why = MR_RW_OTHER;

    if (ppdev == NULL) {
        return TRUE;
    }
    r.left = x1 < x2 ? x1 : x2;
    r.right = (x1 < x2 ? x2 : x1) + 1;
    r.top = y1 < y2 ? y1 : y2;
    r.bottom = (y1 < y2 ? y2 : y1) + 1;
    if (ppdev->psoShadow != NULL) {
        if (mr_inside(ppdev, pco, &r)) {
            if (!EngLineTo(ppdev->psoShadow, pco, pbo, x1, y1, x2, y2,
                           prclBounds, mix)) {
                g_skipped++;
            } else if (!mr_ord_on(ppdev)) {
                why = MR_RW_OFF;
            } else if ((why = mr_line_order(ppdev, pco, pbo, x1, y1, x2, y2,
                                            mix)) == MR_DONE) {
                return TRUE;
            }
        } else {
            g_skipped++;
        }
    }
    mr_rect(ppdev, pco, r.left, r.top, r.right, r.bottom, why);
    return TRUE;
}

static ULONG APIENTRY mrSetPointerShape(SURFOBJ *pso, SURFOBJ *psoMask,
    SURFOBJ *psoColor, XLATEOBJ *pxlo, LONG xHot, LONG yHot, LONG x, LONG y,
    RECTL *prcl, FLONG fl)
{
    mr_ptr_shape(psoMask, psoColor, pxlo, xHot, yHot, x, y, fl);
    return SPS_ACCEPT_NOEXCLUDE;
}

static VOID APIENTRY mrMovePointer(SURFOBJ *pso, LONG x, LONG y, RECTL *prcl)
{
    mr_ptr_move(x, y);
}

static ULONG APIENTRY mrSaveScreenBits(SURFOBJ *pso, ULONG iMode,
    ULONG ident, RECTL *prcl)
{
    MR_PDEV *ppdev = mr_target(pso);
    ULONG    r;

    if (ppdev == NULL || ppdev->psoShadow == NULL) {
        switch (iMode) {
        case SS_SAVE:
            return 1;
        case SS_RESTORE:
            mr_rcl(ppdev, NULL, prcl, MR_RW_RESTORE);
            return TRUE;
        case SS_FREE:
            return TRUE;
        }
        return 0;
    }
    switch (iMode) {
    case SS_SAVE:
        r = mr_save(ppdev, prcl);
        if (r != 0) {
            g_saves++;
            mr_save_order(ppdev, &ppdev->save[r - 1]);
        } else {
            g_savefail++;
        }
        return r;
    case SS_RESTORE:
        if (!mr_restore(ppdev, ident, prcl)) {
            g_savefail++;
            return FALSE;
        }
        return TRUE;
    case SS_FREE:
        if (ident != 0 && ident <= MR_SAVES) {
            mr_save_free(&ppdev->save[ident - 1]);
        }
        return TRUE;
    }
    return 0;
}

static ULONG APIENTRY mrEscape(SURFOBJ *pso, ULONG iEsc, ULONG cjIn,
    PVOID pvIn, ULONG cjOut, PVOID pvOut)
{
    MR_PDEV *ppdev = NULL;
    HDEV     hdev = NULL;
    ULONG    diag = 0;

    if (iEsc == QUERYESCSUPPORT) {
        return (pvIn != NULL && cjIn >= sizeof(ULONG) &&
                *(ULONG *) pvIn == MR_ESCAPE) ? 1 : 0;
    }
    if (iEsc != MR_ESCAPE || pvIn == NULL || cjIn < MR_IN_MIN ||
        pvOut == NULL || cjOut < sizeof(MR_OUT)) {
        return 0;
    }
    if (pso != NULL) {
        ppdev = mr_find((PVOID) pso->dhpdev);
        hdev = pso->hdev;
    }
    if (ppdev != NULL) {
        diag |= MR_D_OWNSURF;
    } else {
        ppdev = g_cur;
    }
#ifdef MR_RTM
    diag |= MR_D_NOEVENT;
    diag |= MR_D_SWPOINTER;
#endif
    return mr_escape(ppdev, hdev, diag, (MR_IN *) pvIn, cjOut, pvOut);
}

static const DRVFN g_fns[] = {
    { INDEX_DrvEnablePDEV,          (PFN) mrEnablePDEV },
    { INDEX_DrvCompletePDEV,        (PFN) mrCompletePDEV },
    { INDEX_DrvDisablePDEV,         (PFN) mrDisablePDEV },
    { INDEX_DrvEnableSurface,       (PFN) mrEnableSurface },
    { INDEX_DrvDisableSurface,      (PFN) mrDisableSurface },
    { INDEX_DrvAssertMode,          (PFN) mrAssertMode },
    { INDEX_DrvStrokePath,          (PFN) mrStrokePath },
    { INDEX_DrvFillPath,            (PFN) mrFillPath },
    { INDEX_DrvStrokeAndFillPath,   (PFN) mrStrokeAndFillPath },
    { INDEX_DrvPaint,               (PFN) mrPaint },
    { INDEX_DrvBitBlt,              (PFN) mrBitBlt },
    { INDEX_DrvCopyBits,            (PFN) mrCopyBits },
    { INDEX_DrvStretchBlt,          (PFN) mrStretchBlt },
    { INDEX_DrvSetPalette,          (PFN) mrSetPalette },
    { INDEX_DrvTextOut,             (PFN) mrTextOut },
    { INDEX_DrvEscape,              (PFN) mrEscape },
    { INDEX_DrvSetPointerShape,     (PFN) mrSetPointerShape },
    { INDEX_DrvMovePointer,         (PFN) mrMovePointer },
    { INDEX_DrvLineTo,              (PFN) mrLineTo },
    { INDEX_DrvSaveScreenBits,      (PFN) mrSaveScreenBits },
    { INDEX_DrvGetModes,            (PFN) mrGetModes }
};

BOOL APIENTRY DrvEnableDriver(ULONG iEngineVersion, ULONG cj,
                              DRVENABLEDATA *pded)
{
#ifdef MR_RTM
    if (iEngineVersion < MR_DDI_RTM || iEngineVersion >= MR_DDI_NT5) {
        return FALSE;
    }
#else
    if (iEngineVersion < MR_DDI_SP3 || iEngineVersion >= MR_DDI_NT5) {
        return FALSE;
    }
#endif
    if (pded == NULL || cj < sizeof(DRVENABLEDATA)) {
        return FALSE;
    }
#if !defined(MR_RTM) && !defined(_X86_)
    if (g_sem == NULL) {
        g_sem = EngCreateSemaphore();
        if (g_sem == NULL) {
            return FALSE;
        }
    }
#endif
    g_engine = iEngineVersion;
#ifdef MR_RTM
    pded->iDriverVersion = MR_DDI_RTM;
#else
    pded->iDriverVersion = MR_DDI_SP3;
#endif
    pded->c = sizeof(g_fns) / sizeof(g_fns[0]);
    pded->pdrvfn = (DRVFN *) g_fns;
    return TRUE;
}
