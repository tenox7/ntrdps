/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <stddef.h>
#include <windows.h>
#include <winddi.h>

#include "shadow.h"

#ifndef INDEX_DrvHookDriver
#define LY_NT31         1
#endif

#define LY_NFUNC        INDEX_LAST
#define LY_ENGINE_MAX   0x00020000L
#define LY_GLYPHS       1024

typedef DHPDEV (APIENTRY *LYF_ENABLEPDEV)(DEVMODEW *, PWSTR, ULONG, HSURF *,
    ULONG, ULONG *, ULONG, DEVINFO *, PWSTR, PWSTR, HANDLE);
typedef VOID  (APIENTRY *LYF_COMPLETEPDEV)(DHPDEV, HDEV);
typedef VOID  (APIENTRY *LYF_DHPDEV)(DHPDEV);
typedef HSURF (APIENTRY *LYF_ENABLESURFACE)(DHPDEV);
typedef VOID  (APIENTRY *LYF_ASSERTMODE)(DHPDEV, BOOL);
typedef BOOL  (APIENTRY *LYF_STROKEPATH)(SURFOBJ *, PATHOBJ *, CLIPOBJ *,
    XFORMOBJ *, BRUSHOBJ *, POINTL *, LINEATTRS *, MIX);
typedef BOOL  (APIENTRY *LYF_FILLPATH)(SURFOBJ *, PATHOBJ *, CLIPOBJ *,
    BRUSHOBJ *, POINTL *, MIX, FLONG);
typedef BOOL  (APIENTRY *LYF_STROKEANDFILLPATH)(SURFOBJ *, PATHOBJ *,
    CLIPOBJ *, XFORMOBJ *, BRUSHOBJ *, LINEATTRS *, BRUSHOBJ *, POINTL *,
    MIX, FLONG);
typedef BOOL  (APIENTRY *LYF_PAINT)(SURFOBJ *, CLIPOBJ *, BRUSHOBJ *,
    POINTL *, MIX);
typedef BOOL  (APIENTRY *LYF_BITBLT)(SURFOBJ *, SURFOBJ *, SURFOBJ *,
    CLIPOBJ *, XLATEOBJ *, RECTL *, POINTL *, POINTL *, BRUSHOBJ *,
    POINTL *, ROP4);
typedef BOOL  (APIENTRY *LYF_COPYBITS)(SURFOBJ *, SURFOBJ *, CLIPOBJ *,
    XLATEOBJ *, RECTL *, POINTL *);
typedef BOOL  (APIENTRY *LYF_STRETCHBLT)(SURFOBJ *, SURFOBJ *, SURFOBJ *,
    CLIPOBJ *, XLATEOBJ *, COLORADJUSTMENT *, POINTL *, RECTL *, RECTL *,
    POINTL *, ULONG);
typedef BOOL  (APIENTRY *LYF_SETPALETTE)(DHPDEV, PALOBJ *, FLONG, ULONG,
    ULONG);
typedef BOOL  (APIENTRY *LYF_TEXTOUT)(SURFOBJ *, STROBJ *, FONTOBJ *,
    CLIPOBJ *, RECTL *, RECTL *, BRUSHOBJ *, BRUSHOBJ *, POINTL *, MIX);
typedef ULONG (APIENTRY *LYF_ESCAPE)(SURFOBJ *, ULONG, ULONG, PVOID, ULONG,
    PVOID);
typedef ULONG (APIENTRY *LYF_SETPOINTERSHAPE)(SURFOBJ *, SURFOBJ *,
    SURFOBJ *, XLATEOBJ *, LONG, LONG, LONG, LONG, RECTL *, FLONG);
typedef VOID  (APIENTRY *LYF_MOVEPOINTER)(SURFOBJ *, LONG, LONG, RECTL *);
typedef ULONG (APIENTRY *LYF_SAVESCREENBITS)(SURFOBJ *, ULONG, ULONG,
    RECTL *);

#define REAL(type, i)   ((type) g_real[i])

#ifdef LY_NT31
typedef BOOL (APIENTRY *LYF_ENABLEDRIVER)(ULONG, ULONG, DRVENABLEDATA *);
typedef VOID (APIENTRY *LYF_DISABLEDRIVER)(VOID);
#endif

typedef struct LY_USTR {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} LY_USTR;

typedef struct LY_OBJATTR {
    ULONG    Length;
    HANDLE   RootDirectory;
    LY_USTR *ObjectName;
    ULONG    Attributes;
    PVOID    SecurityDescriptor;
    PVOID    SecurityQualityOfService;
} LY_OBJATTR;

typedef struct LY_KVPI {
    ULONG TitleIndex;
    ULONG Type;
    ULONG DataLength;
    ULONG Data[256];
} LY_KVPI;

typedef LONG (WINAPI *LYF_NTOPENKEY)(HANDLE *, ACCESS_MASK, LY_OBJATTR *);
typedef LONG (WINAPI *LYF_NTQUERYVALUEKEY)(HANDLE, LY_USTR *, ULONG, PVOID,
    ULONG, ULONG *);
typedef LONG (WINAPI *LYF_NTCLOSE)(HANDLE);

#ifdef LY_NT31
typedef LONG (WINAPI *LYF_NTOPENLINK)(HANDLE *, ACCESS_MASK, LY_OBJATTR *);
typedef LONG (WINAPI *LYF_NTQUERYLINK)(HANDLE, LY_USTR *, ULONG *);
#endif

#define LY_OBJ_CASE_INSENSITIVE     0x00000040L
#define LY_KEYVALUEPARTIAL          2

static const WCHAR g_key[] =
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\ntrdps";
static const WCHAR g_value[] = L"LayerDriver";

static LYF_NTOPENKEY       g_ntopen;
static LYF_NTQUERYVALUEKEY g_ntquery;
static LYF_NTCLOSE         g_ntclose;
static LY_KVPI             g_kv;
#ifdef LY_NT31
static LYF_NTOPENLINK      g_ntopenlink;
static LYF_NTQUERYLINK     g_ntquerylink;
#endif

static PFN              g_real[LY_NFUNC];
static WCHAR            g_name[32];
static BOOL             g_hooked;
static BOOL             g_init;
static CRITICAL_SECTION g_lock;
static LONG             g_nest;
static HANDLE           g_event;
static HANDLE           g_owner;
static HANDLE           g_ownerid;
#ifdef LY_NT31
static HMODULE          g_hmod;
static DRVFN            g_fn[LY_NFUNC];
static BOOL             g_on;
static ULONG            g_caller;
static ULONG            g_diag31;
static ULONG            g_dm[64];
static WCHAR            g_link[] = L"\\DosDevices\\DISPLAY1";
static WCHAR            g_target[128];
static WCHAR            g_path[256];
static WCHAR            g_names[256];
#define LY_CALLER()     ((HANDLE) g_caller)
#define LY_DIAG()       g_diag31
#define LY_ON()         g_on
#else
#define LY_CALLER()     EngGetProcessHandle()
#define LY_DIAG()       0
#define LY_ON()         TRUE
#endif
static POINTL           g_ptl0[LY_GLYPHS];
static POINTL          *g_ptl = g_ptl0;
static ULONG            g_ptlmax = LY_GLYPHS;

PVOID mr_alloc(ULONG cb)
{
    return VirtualAlloc(NULL, cb, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

VOID mr_free(PVOID pv)
{
    if (pv != NULL) {
        VirtualFree(pv, 0, MEM_RELEASE);
    }
}

VOID mr_signal(VOID)
{
    if (g_event != NULL) {
        SetEvent(g_event);
    }
}

MR_PDEV *mr_target(SURFOBJ *pso)
{
    ULONG i;

    if (pso == NULL) {
        return NULL;
    }
    for (i = 0; i < MR_MAXPDEV; i++) {
        if (g_pdev[i] != NULL && g_pdev[i]->hsurf != NULL &&
            g_pdev[i]->hsurf == pso->hsurf) {
            return g_pdev[i];
        }
    }
    return NULL;
}

static BOOL ly_owner_alive(void)
{
    return g_owner != NULL && WaitForSingleObject(g_owner, 0) == WAIT_TIMEOUT;
}

static void ly_chan_close(void)
{
    if (g_event != NULL) {
        CloseHandle(g_event);
    }
    if (g_owner != NULL) {
        CloseHandle(g_owner);
    }
    g_event = NULL;
    g_owner = NULL;
    g_ownerid = NULL;
}

ULONG mr_chan_stop(VOID)
{
    if (g_event != NULL && ly_owner_alive() &&
        LY_CALLER() != g_ownerid) {
        return MR_BUSY;
    }
    mr_stop_reset();
    ly_chan_close();
    return MR_OK;
}

ULONG mr_chan_start(MR_PDEV *ppdev, HDEV hdev, ULONG arg)
{
    HANDLE self = GetCurrentProcess();
    HANDLE proc;
    HANDLE ev = NULL, own = NULL;
    ULONG  st;

    if (ppdev->diag & MR_D_BMPSCREEN) {
        return MR_NOSCREEN;
    }
    if (g_event != NULL) {
        st = mr_chan_stop();
        if (st != MR_OK) {
            return st;
        }
    }
#ifdef LY_NT31
    proc = (g_caller == 0 || arg == 0) ? NULL :
           OpenProcess(PROCESS_DUP_HANDLE | SYNCHRONIZE, FALSE, g_caller);
    if (proc == NULL) {
        return MR_BADEVENT;
    }
    if (!DuplicateHandle(proc, (HANDLE) arg, self, &ev, EVENT_MODIFY_STATE,
                         FALSE, 0)) {
        CloseHandle(proc);
        return MR_BADEVENT;
    }
    own = proc;
    g_ownerid = LY_CALLER();
#else
    proc = EngGetProcessHandle();
    if (proc == NULL || arg == 0) {
        return MR_BADEVENT;
    }
    if (!DuplicateHandle(proc, (HANDLE) arg, self, &ev, EVENT_MODIFY_STATE,
                         FALSE, 0)) {
        return MR_BADEVENT;
    }
    if (!DuplicateHandle(self, proc, self, &own, SYNCHRONIZE, FALSE, 0)) {
        CloseHandle(ev);
        return MR_BADEVENT;
    }
    g_ownerid = proc;
#endif
    g_event = ev;
    g_owner = own;
    mr_start_reset(ppdev);
    return MR_OK;
}

ULONG mr_chan_check(VOID)
{
    if (g_event == NULL) {
        return MR_STOPPED;
    }
    if (!ly_owner_alive()) {
        mr_stop_reset();
        ly_chan_close();
        return MR_STOPPED;
    }
    return LY_CALLER() == g_ownerid ? MR_OK : MR_BUSY;
}

static BOOL ly_enter(void)
{
    EnterCriticalSection(&g_lock);
    g_nest++;
    return g_nest == 1;
}

static void ly_leave(void)
{
    g_nest--;
    LeaveCriticalSection(&g_lock);
}

static MR_PDEV *ly_pdev(DHPDEV dhpdev)
{
    ULONG i;

    for (i = 0; i < MR_MAXPDEV; i++) {
        if (g_pdev[i] != NULL && g_pdev[i]->dhpdevReal == dhpdev) {
            return g_pdev[i];
        }
    }
    return NULL;
}

static BOOL ly_nt(void)
{
    HMODULE nt;

    if (g_ntopen != NULL && g_ntquery != NULL && g_ntclose != NULL) {
        return TRUE;
    }
    nt = GetModuleHandleA("ntdll.dll");
    if (nt == NULL) {
        return FALSE;
    }
    g_ntopen = (LYF_NTOPENKEY) GetProcAddress(nt, "NtOpenKey");
    g_ntquery = (LYF_NTQUERYVALUEKEY) GetProcAddress(nt, "NtQueryValueKey");
    g_ntclose = (LYF_NTCLOSE) GetProcAddress(nt, "NtClose");
#ifdef LY_NT31
    g_ntopenlink = (LYF_NTOPENLINK) GetProcAddress(nt,
                                                   "NtOpenSymbolicLinkObject");
    g_ntquerylink = (LYF_NTQUERYLINK) GetProcAddress(nt,
                                                 "NtQuerySymbolicLinkObject");
    if (g_ntopenlink == NULL || g_ntquerylink == NULL) {
        return FALSE;
    }
#endif
    return g_ntopen != NULL && g_ntquery != NULL && g_ntclose != NULL;
}

static void ly_ustr(LY_USTR *u, const WCHAR *s)
{
    ULONG n = 0;

    while (s[n] != 0 && n < 1000) {
        n++;
    }
    u->Buffer = (PWSTR) s;
    u->Length = (USHORT) (n * sizeof(WCHAR));
    u->MaximumLength = (USHORT) ((n + 1) * sizeof(WCHAR));
}

static void ly_attr(LY_OBJATTR *oa, LY_USTR *name)
{
    oa->Length = sizeof(*oa);
    oa->RootDirectory = NULL;
    oa->ObjectName = name;
    oa->Attributes = LY_OBJ_CASE_INSENSITIVE;
    oa->SecurityDescriptor = NULL;
    oa->SecurityQualityOfService = NULL;
}

static HANDLE ly_open(const WCHAR *path)
{
    LY_USTR    key;
    LY_OBJATTR oa;
    HANDLE     hkey = NULL;

    if (!ly_nt()) {
        return NULL;
    }
    ly_ustr(&key, path);
    ly_attr(&oa, &key);
    if (g_ntopen(&hkey, KEY_QUERY_VALUE, &oa) < 0) {
        return NULL;
    }
    return hkey;
}

static BOOL ly_query(HANDLE hkey, const WCHAR *name)
{
    LY_USTR val;
    ULONG   got = 0;

    ly_ustr(&val, name);
    g_kv.Type = REG_NONE;
    g_kv.DataLength = 0;
    return g_ntquery(hkey, &val, LY_KEYVALUEPARTIAL, &g_kv, sizeof(g_kv),
                     &got) >= 0;
}

static BOOL ly_switched_on(void)
{
    HANDLE hkey = ly_open(g_key);
    BOOL   on;

    if (hkey == NULL) {
        return FALSE;
    }
    on = ly_query(hkey, g_value) && g_kv.Type == REG_DWORD &&
         g_kv.DataLength == 4 && g_kv.Data[0] != 0;
    g_ntclose(hkey);
    return on;
}

static BOOL ly_ptl_room(ULONG n)
{
    POINTL *p;
    ULONG   max;

    if (n <= g_ptlmax) {
        return TRUE;
    }
    if (n > 0x00100000) {
        return FALSE;
    }
    max = (n + 8191) & ~8191UL;
    p = (POINTL *) mr_alloc(max * sizeof(POINTL));
    if (p == NULL) {
        return FALSE;
    }
    if (g_ptl != g_ptl0) {
        mr_free(g_ptl);
    }
    g_ptl = p;
    g_ptlmax = max;
    return TRUE;
}

static BOOL ly_glyphs(STROBJ *pstro, BOOL put)
{
    GLYPHPOS *pgp;
    ULONG     n = pstro->cGlyphs, i = 0, c, k;
    BOOL      more;

    if (!put && !ly_ptl_room(n)) {
        return FALSE;
    }
    if (pstro->pgp != NULL) {
        for (k = 0; k < n; k++) {
            if (put) {
                pstro->pgp[k].ptl = g_ptl[k];
            } else {
                g_ptl[k] = pstro->pgp[k].ptl;
            }
        }
        return TRUE;
    }
    STROBJ_vEnumStart(pstro);
    do {
        c = 0;
        pgp = NULL;
        more = STROBJ_bEnum(pstro, &c, &pgp);
        for (k = 0; pgp != NULL && k < c && i < n; k++, i++) {
            if (put) {
                pgp[k].ptl = g_ptl[i];
            } else {
                g_ptl[i] = pgp[k].ptl;
            }
        }
    } while (more && c != 0 && i < n);
    STROBJ_vEnumStart(pstro);
    return TRUE;
}

VOID mr_glyphs_back(STROBJ *pstro)
{
    ly_glyphs(pstro, TRUE);
}

static VOID ly_pdev_new(DHPDEV dhpdev, GDIINFO *pgi)
{
    MR_PDEV *ppdev;
    ULONG    slot, bpp = pgi->cBitsPixel * pgi->cPlanes, fmt;
    LONG     cx = (LONG) pgi->ulHorzRes, cy = (LONG) pgi->ulVertRes;

    if (bpp == 15) {
        bpp = 16;
    }
    switch (bpp) {
    case 4:  fmt = BMF_4BPP;  break;
    case 8:  fmt = BMF_8BPP;  break;
    case 16: fmt = BMF_16BPP; break;
    case 24: fmt = BMF_24BPP; break;
    case 32: fmt = BMF_32BPP; break;
    default: return;
    }
    if (cx <= 0 || cy <= 0 || cx > 32767 || cy > 32767) {
        return;
    }
    for (slot = 0; slot < MR_MAXPDEV && g_pdev[slot] != NULL; slot++) {
    }
    if (slot == MR_MAXPDEV) {
        return;
    }
    ppdev = (MR_PDEV *) mr_alloc(sizeof(MR_PDEV));
    if (ppdev == NULL) {
        return;
    }
    ppdev->dhpdevReal = dhpdev;
    ppdev->cx = cx;
    ppdev->cy = cy;
    ppdev->bpp = bpp;
    ppdev->iFormat = fmt;
    ppdev->diag = MR_D_LAYER | MR_D_GDIINFO;
    rec_init(&ppdev->rec, cx, cy);
    g_pdev[slot] = ppdev;
}

static VOID ly_surface(MR_PDEV *ppdev, HSURF hsurf)
{
    SURFOBJ *pso = EngLockSurface(hsurf);
    ULONG    type, fmt;
    SIZEL    sizl;

    if (pso == NULL) {
        return;
    }
    type = pso->iType;
    fmt = pso->iBitmapFormat;
    sizl = pso->sizlBitmap;
    EngUnlockSurface(pso);
    if (type != STYPE_DEVICE) {
        ppdev->diag |= MR_D_BMPSCREEN;
        g_cur = ppdev;
        return;
    }
    if ((fmt != 0 && fmt != ppdev->iFormat) || sizl.cx != ppdev->cx ||
        sizl.cy != ppdev->cy) {
        return;
    }
    ppdev->hsurf = hsurf;
    mr_shadow_make(ppdev);
    g_cur = ppdev;
    if (rec_flag(&ppdev->rec, MR_F_ALL | MR_F_MODE)) {
        mr_signal();
    }
}

#ifdef LY_NT31
static BOOL ly31_devmode(DEVMODEW **ppdm);
#endif

static DHPDEV APIENTRY lyEnablePDEV(DEVMODEW *pdm, PWSTR pwszLogAddress,
    ULONG cPat, HSURF *phsurfPatterns, ULONG cjCaps, ULONG *pdevcaps,
    ULONG cjDevInfo, DEVINFO *pdi, PWSTR pwszDataFile, PWSTR pwszDeviceName,
    HANDLE hDriver)
{
    DHPDEV d;

#ifdef LY_NT31
    if (!ly31_devmode(&pdm)) {
        return NULL;
    }
#endif
    d = REAL(LYF_ENABLEPDEV, INDEX_DrvEnablePDEV)(pdm, pwszLogAddress, cPat,
            phsurfPatterns, cjCaps, pdevcaps, cjDevInfo, pdi, pwszDataFile,
            pwszDeviceName, hDriver);
    if (d != NULL && pdevcaps != NULL && cjCaps >= sizeof(GDIINFO) &&
        LY_ON()) {
        EnterCriticalSection(&g_lock);
        ly_pdev_new(d, (GDIINFO *) pdevcaps);
        LeaveCriticalSection(&g_lock);
    }
    return d;
}

static VOID APIENTRY lyCompletePDEV(DHPDEV dhpdev, HDEV hdev)
{
    MR_PDEV *ppdev;

    REAL(LYF_COMPLETEPDEV, INDEX_DrvCompletePDEV)(dhpdev, hdev);
    EnterCriticalSection(&g_lock);
    ppdev = ly_pdev(dhpdev);
    if (ppdev != NULL) {
        ppdev->hdev = hdev;
    }
    LeaveCriticalSection(&g_lock);
}

static VOID APIENTRY lyDisablePDEV(DHPDEV dhpdev)
{
    MR_PDEV *ppdev;
    ULONG    i;

    EnterCriticalSection(&g_lock);
    ppdev = ly_pdev(dhpdev);
    if (ppdev != NULL) {
        for (i = 0; i < MR_MAXPDEV; i++) {
            if (g_pdev[i] == ppdev) {
                g_pdev[i] = NULL;
            }
        }
        if (g_cur == ppdev) {
            g_cur = NULL;
        }
        mr_shadow_free(ppdev);
        mr_free(ppdev);
    }
    LeaveCriticalSection(&g_lock);
    REAL(LYF_DHPDEV, INDEX_DrvDisablePDEV)(dhpdev);
}

static HSURF APIENTRY lyEnableSurface(DHPDEV dhpdev)
{
    HSURF    hsurf = REAL(LYF_ENABLESURFACE, INDEX_DrvEnableSurface)(dhpdev);
    MR_PDEV *ppdev;

    if (hsurf != NULL) {
        EnterCriticalSection(&g_lock);
        ppdev = ly_pdev(dhpdev);
        if (ppdev != NULL) {
            ly_surface(ppdev, hsurf);
        }
        LeaveCriticalSection(&g_lock);
    }
    return hsurf;
}

static VOID APIENTRY lyDisableSurface(DHPDEV dhpdev)
{
    MR_PDEV *ppdev;

    EnterCriticalSection(&g_lock);
    ppdev = ly_pdev(dhpdev);
    if (ppdev != NULL) {
        if (g_cur == ppdev) {
            g_cur = NULL;
        }
        mr_shadow_free(ppdev);
        ppdev->hsurf = NULL;
    }
    LeaveCriticalSection(&g_lock);
    REAL(LYF_DHPDEV, INDEX_DrvDisableSurface)(dhpdev);
}

static VOID APIENTRY lyAssertMode(DHPDEV dhpdev, BOOL bEnable)
{
    MR_PDEV *ppdev;

    REAL(LYF_ASSERTMODE, INDEX_DrvAssertMode)(dhpdev, bEnable);
    EnterCriticalSection(&g_lock);
    ppdev = ly_pdev(dhpdev);
    if (ppdev != NULL) {
        ppdev->fullscreen = !bEnable;
        if (rec_flag(&ppdev->rec,
                     bEnable ? (MR_F_ALL | MR_F_MODE) : MR_F_MODE)) {
            mr_signal();
        }
    }
    LeaveCriticalSection(&g_lock);
}

static BOOL APIENTRY lySetPalette(DHPDEV dhpdev, PALOBJ *ppalo, FLONG fl,
    ULONG iStart, ULONG cColors)
{
    BOOL     ok = REAL(LYF_SETPALETTE, INDEX_DrvSetPalette)(dhpdev, ppalo, fl,
                     iStart, cColors);
    MR_PDEV *ppdev;

    EnterCriticalSection(&g_lock);
    ppdev = ly_pdev(dhpdev);
    if (ok && ppdev != NULL && rec_flag(&ppdev->rec, MR_F_PALETTE)) {
        mr_signal();
    }
    LeaveCriticalSection(&g_lock);
    return ok;
}

static BOOL APIENTRY lyBitBlt(SURFOBJ *psoTrg, SURFOBJ *psoSrc,
    SURFOBJ *psoMask, CLIPOBJ *pco, XLATEOBJ *pxlo, RECTL *prclTrg,
    POINTL *pptlSrc, POINTL *pptlMask, BRUSHOBJ *pbo, POINTL *pptlBrush,
    ROP4 rop4)
{
    BOOL outer = ly_enter();
    BOOL ok = REAL(LYF_BITBLT, INDEX_DrvBitBlt)(psoTrg, psoSrc, psoMask, pco,
                  pxlo, prclTrg, pptlSrc, pptlMask, pbo, pptlBrush, rop4);

    if (outer && ok) {
        mrBitBlt(psoTrg, psoSrc, psoMask, pco, pxlo, prclTrg, pptlSrc,
                 pptlMask, pbo, pptlBrush, rop4);
    }
    ly_leave();
    return ok;
}

static BOOL APIENTRY lyCopyBits(SURFOBJ *psoDest, SURFOBJ *psoSrc,
    CLIPOBJ *pco, XLATEOBJ *pxlo, RECTL *prclDest, POINTL *pptlSrc)
{
    BOOL outer = ly_enter();
    BOOL ok = REAL(LYF_COPYBITS, INDEX_DrvCopyBits)(psoDest, psoSrc, pco,
                  pxlo, prclDest, pptlSrc);

    if (outer && ok) {
        mrCopyBits(psoDest, psoSrc, pco, pxlo, prclDest, pptlSrc);
    }
    ly_leave();
    return ok;
}

static BOOL APIENTRY lyStretchBlt(SURFOBJ *psoDest, SURFOBJ *psoSrc,
    SURFOBJ *psoMask, CLIPOBJ *pco, XLATEOBJ *pxlo, COLORADJUSTMENT *pca,
    POINTL *pptlHTOrg, RECTL *prclDest, RECTL *prclSrc, POINTL *pptlMask,
    ULONG iMode)
{
    BOOL outer = ly_enter();
    BOOL ok = REAL(LYF_STRETCHBLT, INDEX_DrvStretchBlt)(psoDest, psoSrc,
                  psoMask, pco, pxlo, pca, pptlHTOrg, prclDest, prclSrc,
                  pptlMask, iMode);

    if (outer && ok) {
        mrStretchBlt(psoDest, psoSrc, psoMask, pco, pxlo, pca, pptlHTOrg,
                     prclDest, prclSrc, pptlMask, iMode);
    }
    ly_leave();
    return ok;
}

static BOOL APIENTRY lyPaint(SURFOBJ *pso, CLIPOBJ *pco, BRUSHOBJ *pbo,
    POINTL *pptlBrushOrg, MIX mix)
{
    BOOL outer = ly_enter();
    BOOL ok = REAL(LYF_PAINT, INDEX_DrvPaint)(pso, pco, pbo, pptlBrushOrg,
                                              mix);

    if (outer && ok) {
        mrPaint(pso, pco, pbo, pptlBrushOrg, mix);
    }
    ly_leave();
    return ok;
}

static BOOL APIENTRY lyFillPath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco,
    BRUSHOBJ *pbo, POINTL *pptlBrushOrg, MIX mix, FLONG flOptions)
{
    BOOL outer = ly_enter();
    BOOL ok = REAL(LYF_FILLPATH, INDEX_DrvFillPath)(pso, ppo, pco, pbo,
                  pptlBrushOrg, mix, flOptions);

    if (outer && ok && mr_target(pso) != NULL) {
        if (ppo != NULL) {
            PATHOBJ_vEnumStart(ppo);
        }
        mrFillPath(pso, ppo, pco, pbo, pptlBrushOrg, mix, flOptions);
    }
    ly_leave();
    return ok;
}

static BOOL APIENTRY lyStrokePath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco,
    XFORMOBJ *pxo, BRUSHOBJ *pbo, POINTL *pptlBrushOrg,
    LINEATTRS *plineattrs, MIX mix)
{
    BOOL outer = ly_enter();
    LONG st0 = plineattrs != NULL ? plineattrs->elStyleState.l : 0;
    LONG st1;
    BOOL ok = REAL(LYF_STROKEPATH, INDEX_DrvStrokePath)(pso, ppo, pco, pxo,
                  pbo, pptlBrushOrg, plineattrs, mix);

    if (outer && ok && mr_target(pso) != NULL) {
        st1 = plineattrs != NULL ? plineattrs->elStyleState.l : 0;
        if (plineattrs != NULL) {
            plineattrs->elStyleState.l = st0;
        }
        if (ppo != NULL) {
            PATHOBJ_vEnumStart(ppo);
        }
        mrStrokePath(pso, ppo, pco, pxo, pbo, pptlBrushOrg, plineattrs, mix);
        if (plineattrs != NULL) {
            plineattrs->elStyleState.l = st1;
        }
    }
    ly_leave();
    return ok;
}

static BOOL APIENTRY lyStrokeAndFillPath(SURFOBJ *pso, PATHOBJ *ppo,
    CLIPOBJ *pco, XFORMOBJ *pxo, BRUSHOBJ *pboStroke, LINEATTRS *plineattrs,
    BRUSHOBJ *pboFill, POINTL *pptlBrushOrg, MIX mixFill, FLONG flOptions)
{
    BOOL outer = ly_enter();
    LONG st0 = plineattrs != NULL ? plineattrs->elStyleState.l : 0;
    LONG st1;
    BOOL ok = REAL(LYF_STROKEANDFILLPATH, INDEX_DrvStrokeAndFillPath)(pso,
                  ppo, pco, pxo, pboStroke, plineattrs, pboFill,
                  pptlBrushOrg, mixFill, flOptions);

    if (outer && ok && mr_target(pso) != NULL) {
        st1 = plineattrs != NULL ? plineattrs->elStyleState.l : 0;
        if (plineattrs != NULL) {
            plineattrs->elStyleState.l = st0;
        }
        if (ppo != NULL) {
            PATHOBJ_vEnumStart(ppo);
        }
        mrStrokeAndFillPath(pso, ppo, pco, pxo, pboStroke, plineattrs,
                            pboFill, pptlBrushOrg, mixFill, flOptions);
        if (plineattrs != NULL) {
            plineattrs->elStyleState.l = st1;
        }
    }
    ly_leave();
    return ok;
}

static BOOL APIENTRY lyTextOut(SURFOBJ *pso, STROBJ *pstro, FONTOBJ *pfo,
    CLIPOBJ *pco, RECTL *prclExtra, RECTL *prclOpaque, BRUSHOBJ *pboFore,
    BRUSHOBJ *pboOpaque, POINTL *pptlOrg, MIX mix)
{
    BOOL     outer = ly_enter();
    MR_PDEV *ppdev = outer ? mr_target(pso) : NULL;
    BOOL     kept = FALSE, ok;
    RECTL    bk, opq;

    bk.left = bk.top = bk.right = bk.bottom = 0;
    opq = bk;
    if (ppdev != NULL && pstro != NULL) {
        kept = ly_glyphs(pstro, FALSE);
        bk = pstro->rclBkGround;
        if (prclOpaque != NULL) {
            opq = *prclOpaque;
        }
    }
    ok = REAL(LYF_TEXTOUT, INDEX_DrvTextOut)(pso, pstro, pfo, pco, prclExtra,
             prclOpaque, pboFore, pboOpaque, pptlOrg, mix);
    if (ok && ppdev != NULL) {
        if (kept) {
            ly_glyphs(pstro, TRUE);
            pstro->rclBkGround = bk;
            if (prclOpaque != NULL) {
                *prclOpaque = opq;
            }
            mrTextOut(pso, pstro, pfo, pco, prclExtra, prclOpaque, pboFore,
                      pboOpaque, pptlOrg, mix);
            ly_glyphs(pstro, TRUE);
        } else if (pstro != NULL) {
            if (ppdev->psoShadow != NULL) {
                g_skipped++;
            }
            mr_rcl(ppdev, pco, &bk, MR_RW_OTHER);
            if (prclOpaque != NULL) {
                mr_rcl(ppdev, pco, &opq, MR_RW_OTHER);
            }
        } else {
            mrTextOut(pso, pstro, pfo, pco, prclExtra, prclOpaque, pboFore,
                      pboOpaque, pptlOrg, mix);
        }
    }
    ly_leave();
    return ok;
}

static ULONG ly_save_find(MR_PDEV *ppdev, ULONG ident)
{
    ULONG i;

    for (i = 0; ident != 0 && i < MR_SAVES; i++) {
        if (ppdev->save[i].hsurf != NULL && ppdev->save[i].realid == ident) {
            return i + 1;
        }
    }
    return 0;
}

static ULONG APIENTRY lySaveScreenBits(SURFOBJ *pso, ULONG iMode,
    ULONG ident, RECTL *prcl)
{
    BOOL     outer = ly_enter();
    MR_PDEV *ppdev = outer ? mr_target(pso) : NULL;
    RECTL    none, rd;
    ULONG    r, k;

    none.left = none.top = none.right = none.bottom = 0;
    if (ppdev == NULL || ppdev->psoShadow == NULL) {
        r = REAL(LYF_SAVESCREENBITS, INDEX_DrvSaveScreenBits)(pso, iMode,
                ident, prcl);
        if (ppdev != NULL && iMode == SS_RESTORE && r != 0 && prcl != NULL) {
            mr_rcl(ppdev, NULL, prcl, MR_RW_RESTORE);
        }
        ly_leave();
        return r;
    }
    switch (iMode) {
    case SS_SAVE:
        r = REAL(LYF_SAVESCREENBITS, INDEX_DrvSaveScreenBits)(pso, SS_SAVE,
                ident, prcl);
        if (r != 0) {
            k = mr_save(ppdev, prcl);
            if (k == 0) {
                REAL(LYF_SAVESCREENBITS, INDEX_DrvSaveScreenBits)(pso,
                    SS_FREE, r, &none);
                g_savefail++;
                r = 0;
            } else {
                ppdev->save[k - 1].realid = r;
                g_saves++;
                mr_save_order(ppdev, &ppdev->save[k - 1]);
            }
        }
        break;
    case SS_RESTORE:
        k = ly_save_find(ppdev, ident);
        if (k == 0 || !mr_restore_area(ppdev, k, prcl, &rd)) {
            REAL(LYF_SAVESCREENBITS, INDEX_DrvSaveScreenBits)(pso, SS_FREE,
                ident, &none);
            if (k != 0) {
                mr_save_free(&ppdev->save[k - 1]);
            }
            g_savefail++;
            r = FALSE;
        } else {
            r = REAL(LYF_SAVESCREENBITS, INDEX_DrvSaveScreenBits)(pso,
                    SS_RESTORE, ident, prcl);
            if (r == 0) {
                mr_save_free(&ppdev->save[k - 1]);
            } else if (!mr_restore(ppdev, k, prcl)) {
                g_savefail++;
            }
        }
        break;
    case SS_FREE:
        r = REAL(LYF_SAVESCREENBITS, INDEX_DrvSaveScreenBits)(pso, SS_FREE,
                ident, prcl);
        k = ly_save_find(ppdev, ident);
        if (k != 0) {
            mr_save_free(&ppdev->save[k - 1]);
        }
        break;
    default:
        r = REAL(LYF_SAVESCREENBITS, INDEX_DrvSaveScreenBits)(pso, iMode,
                ident, prcl);
        break;
    }
    ly_leave();
    return r;
}

static ULONG APIENTRY lySetPointerShape(SURFOBJ *pso, SURFOBJ *psoMask,
    SURFOBJ *psoColor, XLATEOBJ *pxlo, LONG xHot, LONG yHot, LONG x, LONG y,
    RECTL *prcl, FLONG fl)
{
    ULONG r;

    ly_enter();
    r = REAL(LYF_SETPOINTERSHAPE, INDEX_DrvSetPointerShape)(pso, psoMask,
            psoColor, pxlo, xHot, yHot, x, y, prcl, fl);
    if (r == SPS_ACCEPT_NOEXCLUDE || r == SPS_ACCEPT_EXCLUDE) {
        mr_ptr_shape(psoMask, psoColor, pxlo, xHot, yHot, x, y, fl);
    }
    ly_leave();
    return r;
}

static VOID APIENTRY lyMovePointer(SURFOBJ *pso, LONG x, LONG y, RECTL *prcl)
{
    ly_enter();
    REAL(LYF_MOVEPOINTER, INDEX_DrvMovePointer)(pso, x, y, prcl);
    mr_ptr_move(x, y);
    ly_leave();
}

static ULONG APIENTRY lyEscape(SURFOBJ *pso, ULONG iEsc, ULONG cjIn,
    PVOID pvIn, ULONG cjOut, PVOID pvOut)
{
    MR_PDEV *ppdev;
    ULONG    r;

    if (iEsc == QUERYESCSUPPORT && pvIn != NULL && cjIn >= sizeof(ULONG) &&
        *(ULONG *) pvIn == MR_ESCAPE) {
        return 1;
    }
    if (iEsc == MR_ESCAPE) {
        if (pvIn == NULL || cjIn < MR_IN_MIN || pvOut == NULL ||
            cjOut < sizeof(MR_OUT)) {
            return 0;
        }
        EnterCriticalSection(&g_lock);
#ifdef LY_NT31
        g_caller = cjIn >= sizeof(MR_IN) ? ((MR_IN *) pvIn)->pid : 0;
#endif
        ppdev = mr_target(pso);
        if (ppdev == NULL && pso != NULL) {
            ppdev = ly_pdev(pso->dhpdev);
        }
        if (ppdev == NULL) {
            ppdev = g_cur;
        }
        r = mr_escape(ppdev, ppdev != NULL ? ppdev->hdev : NULL,
                      MR_D_LAYER | LY_DIAG(), (MR_IN *) pvIn, cjOut, pvOut);
        LeaveCriticalSection(&g_lock);
        return r;
    }
    if (g_real[INDEX_DrvEscape] == NULL) {
        return 0;
    }
    return REAL(LYF_ESCAPE, INDEX_DrvEscape)(pso, iEsc, cjIn, pvIn, cjOut,
                                             pvOut);
}

static const DRVFN g_wrap[] = {
    { INDEX_DrvEnablePDEV,          (PFN) lyEnablePDEV },
    { INDEX_DrvCompletePDEV,        (PFN) lyCompletePDEV },
    { INDEX_DrvDisablePDEV,         (PFN) lyDisablePDEV },
    { INDEX_DrvEnableSurface,       (PFN) lyEnableSurface },
    { INDEX_DrvDisableSurface,      (PFN) lyDisableSurface },
    { INDEX_DrvAssertMode,          (PFN) lyAssertMode },
    { INDEX_DrvStrokePath,          (PFN) lyStrokePath },
    { INDEX_DrvFillPath,            (PFN) lyFillPath },
    { INDEX_DrvStrokeAndFillPath,   (PFN) lyStrokeAndFillPath },
    { INDEX_DrvPaint,               (PFN) lyPaint },
    { INDEX_DrvBitBlt,              (PFN) lyBitBlt },
    { INDEX_DrvCopyBits,            (PFN) lyCopyBits },
    { INDEX_DrvStretchBlt,          (PFN) lyStretchBlt },
    { INDEX_DrvSetPalette,          (PFN) lySetPalette },
    { INDEX_DrvTextOut,             (PFN) lyTextOut },
    { INDEX_DrvEscape,              (PFN) lyEscape },
    { INDEX_DrvSetPointerShape,     (PFN) lySetPointerShape },
    { INDEX_DrvMovePointer,         (PFN) lyMovePointer },
    { INDEX_DrvSaveScreenBits,      (PFN) lySaveScreenBits }
};

#define LY_NWRAP (sizeof(g_wrap) / sizeof(g_wrap[0]))

static BOOL ly_same(const WCHAR *a, const WCHAR *b)
{
    ULONG i;

    for (i = 0; a[i] != 0 && a[i] == b[i]; i++) {
    }
    return a[i] == b[i];
}

static void ly_wrap(PFN *pfnTable, ULONG n)
{
    ULONG i;

    for (i = 0; i < LY_NWRAP; i++) {
        if (g_wrap[i].iFunc < n && (g_real[g_wrap[i].iFunc] != NULL ||
                                    g_wrap[i].iFunc == INDEX_DrvEscape)) {
            pfnTable[g_wrap[i].iFunc] = g_wrap[i].pfn;
        }
    }
    if (INDEX_DrvDeleteDeviceBitmap < n && INDEX_DrvPlgBlt < n) {
        pfnTable[INDEX_DrvCreateDeviceBitmap] = NULL;
        pfnTable[INDEX_DrvDeleteDeviceBitmap] = NULL;
        pfnTable[INDEX_DrvPlgBlt] = NULL;
    }
}

#ifndef LY_NT31

BOOL APIENTRY DrvHookDriver(PWSZ pwszDriverName, ULONG cFuncs,
                            PFN *pfnTable)
{
    ULONG i, n = cFuncs < LY_NFUNC ? cFuncs : LY_NFUNC;

    if (g_hooked || pfnTable == NULL || !ly_switched_on()) {
        return TRUE;
    }
    for (i = 0; i < LY_NFUNC; i++) {
        g_real[i] = i < n ? pfnTable[i] : NULL;
    }
    ly_wrap(pfnTable, n);
    for (i = 0; pwszDriverName != NULL && pwszDriverName[i] != 0 &&
                i < sizeof(g_name) / sizeof(g_name[0]) - 1; i++) {
        g_name[i] = pwszDriverName[i];
    }
    g_name[i] = 0;
    g_hooked = TRUE;
    return TRUE;
}

VOID APIENTRY DrvUnhookDriver(PWSZ pwszDriverName)
{
    if (g_hooked && pwszDriverName != NULL &&
        ly_same(pwszDriverName, g_name)) {
        g_hooked = FALSE;
    }
}

static const DRVFN g_layer_fn[] = {
    { INDEX_DrvHookDriver,   (PFN) DrvHookDriver },
    { INDEX_DrvUnhookDriver, (PFN) DrvUnhookDriver }
};

BOOL APIENTRY DrvEnableDriver(ULONG iEngineVersion, ULONG cj,
                              DRVENABLEDATA *pded)
{
    if (pded == NULL || cj < sizeof(DRVENABLEDATA) ||
        iEngineVersion < DDI_DRIVER_VERSION ||
        iEngineVersion >= LY_ENGINE_MAX) {
        return FALSE;
    }
    if (!g_init) {
        InitializeCriticalSection(&g_lock);
        g_init = TRUE;
    }
    g_engine = iEngineVersion;
    pded->iDriverVersion = DDI_DRIVER_VERSION;
    pded->c = sizeof(g_layer_fn) / sizeof(g_layer_fn[0]);
    pded->pdrvfn = (DRVFN *) g_layer_fn;
    return TRUE;
}

VOID APIENTRY DrvDisableDriver(VOID)
{
}

#else

#define LY_DM31         0xbc
#define LY_E_LFANEW     0x3c
#define LY_OPTHDR       24
#define LY_NUMDIRS      92
#define LY_DATADIR      96
#define LY_DIR_RESOURCE 2

const char ly_mark[] = "ntrdpsly: ntrdps layered display driver";

static ULONG ly31_text(HANDLE hkey, const WCHAR *name, WCHAR *out, ULONG cch)
{
    const WCHAR *s = (const WCHAR *) g_kv.Data;
    ULONG        n, i;

    out[0] = 0;
    out[1] = 0;
    if (!ly_query(hkey, name) ||
        (g_kv.Type != REG_SZ && g_kv.Type != REG_MULTI_SZ)) {
        return 0;
    }
    n = g_kv.DataLength / sizeof(WCHAR);
    if (n > sizeof(g_kv.Data) / sizeof(WCHAR)) {
        n = sizeof(g_kv.Data) / sizeof(WCHAR);
    }
    if (n > cch - 2) {
        n = cch - 2;
    }
    for (i = 0; i < n; i++) {
        out[i] = s[i];
    }
    out[n] = 0;
    out[n + 1] = 0;
    return g_kv.Type;
}

static BOOL ly31_link(void)
{
    LY_USTR    name, target;
    LY_OBJATTR oa;
    HANDLE     h = NULL;
    LONG       st;

    ly_ustr(&name, g_link);
    ly_attr(&oa, &name);
    if (g_ntopenlink(&h, GENERIC_READ, &oa) < 0) {
        return FALSE;
    }
    target.Buffer = g_target;
    target.Length = 0;
    target.MaximumLength = (USHORT) (sizeof(g_target) - sizeof(WCHAR));
    st = g_ntquerylink(h, &target, NULL);
    g_ntclose(h);
    if (st < 0) {
        return FALSE;
    }
    g_target[target.Length / sizeof(WCHAR)] = 0;
    return TRUE;
}

static HMODULE ly31_find(HMODULE self)
{
    HANDLE  map, dev;
    HMODULE h;
    ULONG   n, i;
    WCHAR  *name;

    map = ly_open(L"\\Registry\\Machine\\Hardware\\DeviceMap\\Video");
    if (map == NULL) {
        return NULL;
    }
    for (n = 1; n <= 4; n++) {
        g_link[19] = (WCHAR) ('0' + n);
        if (!ly31_link() ||
            ly31_text(map, g_target, g_path, 256) != REG_SZ) {
            break;
        }
        dev = ly_open(g_path);
        if (dev == NULL) {
            continue;
        }
        i = ly31_text(dev, L"InstalledDisplayDrivers", g_names, 256);
        g_ntclose(dev);
        if (i == 0) {
            continue;
        }
        for (name = g_names; *name != 0; name += i + 1) {
            for (i = 0; name[i] != 0; i++) {
            }
            h = LoadLibraryW(name);
            if (h == NULL) {
                continue;
            }
            if (h == self || GetProcAddress(h, "DrvEnableDriver") == NULL) {
                FreeLibrary(h);
                continue;
            }
            for (i = 0; name[i] != 0 &&
                        i < sizeof(g_name) / sizeof(g_name[0]) - 1; i++) {
                g_name[i] = name[i];
            }
            g_name[i] = 0;
            g_ntclose(map);
            return h;
        }
    }
    g_ntclose(map);
    return NULL;
}

static HMODULE ly31_self(void)
{
    MEMORY_BASIC_INFORMATION mbi;

    if (VirtualQuery(g_real, &mbi, sizeof(mbi)) == 0) {
        return NULL;
    }
    return (HMODULE) mbi.AllocationBase;
}

static ULONG *ly31_resdir(HMODULE m)
{
    BYTE *b = (BYTE *) m;
    ULONG pe;

    if (b == NULL || b[0] != 'M' || b[1] != 'Z') {
        return NULL;
    }
    pe = *(ULONG *) (b + LY_E_LFANEW);
    if (pe > 0x1000 || b[pe] != 'P' || b[pe + 1] != 'E' || b[pe + 2] != 0 ||
        b[pe + 3] != 0 ||
        *(ULONG *) (b + pe + LY_OPTHDR + LY_NUMDIRS) <= LY_DIR_RESOURCE) {
        return NULL;
    }
    return (ULONG *) (b + pe + LY_OPTHDR + LY_DATADIR + 8 * LY_DIR_RESOURCE);
}

static BOOL ly31_rebase(BYTE *res, const BYTE *orig, ULONG cb, ULONG off,
                        ULONG delta, int depth)
{
    ULONG n, i, o;

    if (depth > 3 || off > cb || cb - off < 16) {
        return FALSE;
    }
    n = (ULONG) *(USHORT *) (res + off + 12) +
        (ULONG) *(USHORT *) (res + off + 14);
    if ((cb - off - 16) / 8 < n) {
        return FALSE;
    }
    for (i = 0; i < n; i++) {
        o = *(ULONG *) (res + off + 16 + 8 * i + 4);
        if (o & 0x80000000UL) {
            if (!ly31_rebase(res, orig, cb, o & 0x7FFFFFFFUL, delta,
                             depth + 1)) {
                return FALSE;
            }
        } else {
            if (o > cb || cb - o < 16) {
                return FALSE;
            }
            *(ULONG *) (res + o) = *(const ULONG *) (orig + o) + delta;
        }
    }
    return TRUE;
}

static BOOL ly31_resources(HMODULE self, HMODULE real)
{
    ULONG *rd = ly31_resdir(real), *sd = ly31_resdir(self), i;
    BYTE  *copy;
    DWORD  old = 0;

    if (rd == NULL || sd == NULL || rd[0] == 0 || rd[1] < 16) {
        return FALSE;
    }
    copy = (BYTE *) mr_alloc(rd[1]);
    if (copy == NULL) {
        return FALSE;
    }
    for (i = 0; i < rd[1]; i++) {
        copy[i] = ((const BYTE *) real)[rd[0] + i];
    }
    if (!ly31_rebase(copy, (const BYTE *) real + rd[0], rd[1], 0,
                     (ULONG) real - (ULONG) self, 0) ||
        (!VirtualProtect(sd, 8, PAGE_READWRITE, &old) &&
         !VirtualProtect(sd, 8, PAGE_WRITECOPY, &old))) {
        mr_free(copy);
        return FALSE;
    }
    sd[0] = (ULONG) copy - (ULONG) self;
    sd[1] = rd[1];
    VirtualProtect(sd, 8, old, &old);
    return TRUE;
}

static WCHAR ly31_upper(WCHAR c)
{
    return (WCHAR) (c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
}

static BOOL ly31_devmode(DEVMODEW **ppdm)
{
    DEVMODEW    *pdm = *ppdm;
    const WCHAR *tail;
    ULONG        i, n;

    if (pdm == NULL || pdm->dmSize <= LY_DM31) {
        return TRUE;
    }
    tail = (const WCHAR *) ((const BYTE *) pdm + LY_DM31);
    n = (pdm->dmSize - LY_DM31) / sizeof(WCHAR);
    for (i = 0; i + 1 < n && tail[i + 1] != 0; i++) {
        if (i >= sizeof(g_name) / sizeof(g_name[0]) - 1 ||
            ly31_upper(tail[i + 1]) != ly31_upper(g_name[i])) {
            return FALSE;
        }
    }
    if (g_name[i] != 0) {
        return FALSE;
    }
    for (i = 0; i < LY_DM31; i++) {
        ((BYTE *) g_dm)[i] = ((const BYTE *) pdm)[i];
    }
    pdm = (DEVMODEW *) g_dm;
    pdm->dmSize = LY_DM31;
    pdm->dmDriverExtra = 0;
    *ppdm = pdm;
    return TRUE;
}

BOOL APIENTRY DrvEnableDriver(ULONG iEngineVersion, ULONG cj,
                              DRVENABLEDATA *pded)
{
    DRVENABLEDATA    real;
    LYF_ENABLEDRIVER enable;
    PFN              tab[LY_NFUNC];
    HMODULE          self;
    ULONG            i, n;

    if (pded == NULL || cj < sizeof(DRVENABLEDATA) || !ly_nt()) {
        return FALSE;
    }
    if (!g_init) {
        InitializeCriticalSection(&g_lock);
        g_init = TRUE;
    }
    if (g_hmod == NULL) {
        self = ly31_self();
        g_hmod = ly31_find(self);
        if (g_hmod == NULL) {
            return FALSE;
        }
        g_on = ly_switched_on();
        g_diag31 = MR_D_NT31;
        if (!ly31_resources(self, g_hmod)) {
            g_diag31 |= MR_D_NORES;
        }
    }
    enable = (LYF_ENABLEDRIVER) GetProcAddress(g_hmod, "DrvEnableDriver");
    real.iDriverVersion = 0;
    real.c = 0;
    real.pdrvfn = NULL;
    if (enable == NULL || !enable(iEngineVersion, sizeof(real), &real) ||
        real.pdrvfn == NULL) {
        return FALSE;
    }
    for (i = 0; i < LY_NFUNC; i++) {
        g_real[i] = NULL;
    }
    for (i = 0; i < real.c; i++) {
        if (real.pdrvfn[i].iFunc < LY_NFUNC) {
            g_real[real.pdrvfn[i].iFunc] = real.pdrvfn[i].pfn;
        }
    }
    for (i = 0; i < LY_NFUNC; i++) {
        tab[i] = g_real[i];
    }
    if (g_on) {
        ly_wrap(tab, LY_NFUNC);
    } else if (tab[INDEX_DrvEnablePDEV] != NULL) {
        tab[INDEX_DrvEnablePDEV] = (PFN) lyEnablePDEV;
    }
    for (i = 0, n = 0; i < LY_NFUNC; i++) {
        if (tab[i] != NULL) {
            g_fn[n].iFunc = i;
            g_fn[n].pfn = tab[i];
            n++;
        }
    }
    g_engine = iEngineVersion;
    pded->iDriverVersion = real.iDriverVersion;
    pded->c = n;
    pded->pdrvfn = g_fn;
    return TRUE;
}

VOID APIENTRY DrvDisableDriver(VOID)
{
    LYF_DISABLEDRIVER disable;

    if (g_hmod != NULL) {
        disable = (LYF_DISABLEDRIVER) GetProcAddress(g_hmod,
                                                     "DrvDisableDriver");
        if (disable != NULL) {
            disable();
        }
    }
}

#endif
