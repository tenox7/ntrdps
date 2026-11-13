/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <string.h>
#include "log.h"
#include "mrchan.h"
#include "mirror.h"

static HDC          g_dc;
static HANDLE       g_event;
static int          g_layered;
static int          g_noevent;
static int          g_swpointer;
static MR_OUT       g_out;
static MIRROR_STATS g_st;
static unsigned long g_rwbase[MR_RW_N];
static unsigned long g_rpbase[MR_RW_N];

static int escape(ULONG cmd, ULONG arg)
{
    MR_IN in;
    int rc;

    in.magic = MR_MAGIC;
    in.version = MR_VERSION;
    in.pid = GetCurrentProcessId();
    in.cmd = cmd;
    in.arg = arg;
    memset(&g_out, 0, sizeof(g_out));
    rc = ExtEscape(g_dc, MR_ESCAPE, sizeof(in), (LPCSTR) &in, sizeof(g_out),
                   (LPSTR) &g_out);
    return rc != 0 && g_out.magic == MR_MAGIC && g_out.version == MR_VERSION;
}

static void copy_counts(void)
{
    g_st.calls = g_out.calls;
    g_st.reads = g_out.reads;
    g_st.ptrshape = g_out.ptrshape;
    g_st.ptrmove = g_out.ptrmove;
}

static void close_handles(void)
{
    if (g_event != NULL) {
        CloseHandle(g_event);
        g_event = NULL;
    }
    if (g_dc != NULL) {
        DeleteDC(g_dc);
        g_dc = NULL;
    }
}

static int driver_kind(void)
{
    DWORD v = GetVersion();
    DWORD major = v & 0xFF, minor = (v >> 8) & 0xFF;

    if (v & 0x80000000UL) {
        return 0;
    }
    if (major == 4) {
        return 1;
    }
#ifdef NTRDPS_TLS
    if (major == 5 && minor == 0) {
        return 1;
    }
#endif
    return major == 3 && (minor == 10 || minor == 50 || minor == 51) ? 2 :
           0;
}

#ifdef NTRDPS_TLS
typedef struct {
    DWORD cb;
    CHAR  DeviceName[32];
    CHAR  DeviceString[128];
    DWORD StateFlags;
    CHAR  DeviceID[128];
    CHAR  DeviceKey[128];
} NT5_DEVICE;
typedef BOOL (WINAPI *NT5_ENUMDEV)(LPCSTR, DWORD, NT5_DEVICE *, DWORD);
typedef BOOL (WINAPI *NT5_ENUMSET)(LPCSTR, DWORD, DEVMODEA *);
typedef LONG (WINAPI *NT5_CHANGE)(LPCSTR, DEVMODEA *, HWND, DWORD,
                                  LPVOID);
#define NT5_CURRENT     ((DWORD) -1)
#define NT5_FIELDS      0x001C0020UL
#define NT5_UPDATEREG   0x00000001UL

static int   g_nt5;
static DWORD g_fitw, g_fith, g_fitb;

static int our_device(NT5_ENUMDEV enumdev, NT5_DEVICE *dd)
{
    char key[sizeof(dd->DeviceKey)];
    DWORD i;
    int k;

    for (i = 0; i < 64; i++) {
        memset(dd, 0, sizeof(*dd));
        dd->cb = sizeof(*dd);
        if (!enumdev(NULL, i, dd, 0)) {
            break;
        }
        for (k = 0; k < (int) sizeof(key) - 1 && dd->DeviceKey[k]; k++) {
            key[k] = dd->DeviceKey[k];
            if (key[k] >= 'a' && key[k] <= 'z') {
                key[k] = (char) (key[k] - 'a' + 'A');
            }
        }
        key[k] = '\0';
        if (strstr(key, "\\SERVICES\\NTRDPSMR\\") != NULL) {
            return 1;
        }
    }
    return 0;
}

static int fit_mirror(void)
{
    HMODULE u;
    NT5_ENUMDEV enumdev;
    NT5_ENUMSET enumset;
    NT5_CHANGE change;
    NT5_DEVICE dd;
    DEVMODEA scr, mir;
    LONG rc;

    u = GetModuleHandleA("user32.dll");
    if (!g_nt5 || u == NULL) {
        return 1;
    }
    enumdev = (NT5_ENUMDEV) GetProcAddress(u, "EnumDisplayDevicesA");
    enumset = (NT5_ENUMSET) GetProcAddress(u, "EnumDisplaySettingsA");
    change = (NT5_CHANGE) GetProcAddress(u, "ChangeDisplaySettingsExA");
    memset(&scr, 0, sizeof(scr));
    scr.dmSize = sizeof(scr);
    memset(&mir, 0, sizeof(mir));
    mir.dmSize = sizeof(mir);
    if (enumdev == NULL || enumset == NULL || change == NULL ||
        !our_device(enumdev, &dd) || !enumset(NULL, NT5_CURRENT, &scr) ||
        !enumset(dd.DeviceName, NT5_CURRENT, &mir) ||
        (mir.dmPelsWidth == scr.dmPelsWidth &&
         mir.dmPelsHeight == scr.dmPelsHeight &&
         mir.dmBitsPerPel == scr.dmBitsPerPel)) {
        return 1;
    }
    if (scr.dmPelsWidth == g_fitw && scr.dmPelsHeight == g_fith &&
        scr.dmBitsPerPel == g_fitb) {
        return 0;
    }
    g_fitw = scr.dmPelsWidth;
    g_fith = scr.dmPelsHeight;
    g_fitb = scr.dmBitsPerPel;
    scr.dmOrientation = 0;
    scr.dmPaperSize = 0;
    scr.dmPaperLength = 0;
    scr.dmPaperWidth = 0;
    scr.dmFields = NT5_FIELDS;
    rc = change(dd.DeviceName, &scr, NULL, NT5_UPDATEREG, NULL);
    log_msg(rc == 0 ? LOG_INFO : LOG_WARN, "mirror: the mirror driver has "
            "%lu x %lu, %lu bpp, the screen %lu x %lu, %lu bpp (Windows "
            "2000 gives a mirror its saved mode): ChangeDisplaySettingsEx "
            "on %s gave %ld%s", (unsigned long) mir.dmPelsWidth,
            (unsigned long) mir.dmPelsHeight,
            (unsigned long) mir.dmBitsPerPel, (unsigned long) g_fitw,
            (unsigned long) g_fith, (unsigned long) g_fitb, dd.DeviceName,
            (long) rc, rc == 0 ? " (ok: the mirror now has the screen's "
            "mode)" : "; polling");
    return rc == 0;
}
#endif

int mirror_open(void)
{
    ULONG q = MR_ESCAPE;
    int kind = driver_kind();

    if (g_event != NULL) {
        return 1;
    }
    memset(&g_st, 0, sizeof(g_st));
    if (kind == 0) {
        return 0;
    }
    g_dc = CreateDCA("DISPLAY", NULL, NULL, NULL);
    if (g_dc == NULL) {
        log_msg(LOG_WARN, "mirror: CreateDC(DISPLAY) failed, error %lu",
                (unsigned long) GetLastError());
        return 0;
    }
    if (ExtEscape(g_dc, QUERYESCSUPPORT, sizeof(q), (LPCSTR) &q, 0,
                  NULL) == 0) {
        if (kind == 2) {
            log_msg(LOG_INFO, "mirror: no layered driver (ntrdps "
                    "-installlayer installs it on NT 3.1, 3.5 and 3.51)");
        } else {
#ifdef NTRDPS_TLS
            log_msg(LOG_INFO, "mirror: no mirror driver (ntrdps "
                    "-installmirror installs it on NT 4.0 and Windows "
                    "2000)");
#else
            log_msg(LOG_INFO, "mirror: no mirror driver (ntrdps "
                    "-installmirror installs it on NT 4.0)");
#endif
        }
        close_handles();
        return 0;
    }
    g_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (g_event == NULL) {
        log_msg(LOG_WARN, "mirror: CreateEvent failed, error %lu",
                (unsigned long) GetLastError());
        close_handles();
        return 0;
    }
    if (!escape(MR_CMD_START, (ULONG) g_event) || g_out.status != MR_OK) {
        if (g_out.magic == MR_MAGIC && g_out.status == MR_NOSCREEN) {
            log_msg(LOG_INFO, "mirror: the layered driver cannot follow "
                    "this display driver (its screen is an engine "
                    "bitmap); polling");
        } else {
            log_msg(LOG_WARN, "mirror: the driver did not open the "
                    "channel (answer %s, status %lu); polling",
                    g_out.magic == MR_MAGIC ? "ok" : "none",
                    (unsigned long) g_out.status);
        }
        close_handles();
        return 0;
    }
    copy_counts();
    g_layered = (g_out.diag & MR_D_LAYER) != 0;
    g_noevent = (g_out.diag & MR_D_NOEVENT) != 0;
    g_swpointer = (g_out.diag & MR_D_SWPOINTER) != 0;
    log_msg(LOG_INFO, "mirror: channel open (%s driver): %ld x %ld, %lu "
            "bpp, engine 0x%lx, diag 0x%lx%s%s",
            (g_out.diag & MR_D_LAYER) ? "layered" : "mirror",
            (long) g_out.cx, (long) g_out.cy, (unsigned long) g_out.bpp,
            (unsigned long) g_out.engine, (unsigned long) g_out.diag,
#ifdef NTRDPS_TLS
            g_noevent ? "; no event (NT 4.0 before SP3, Windows 2000): "
                        "asked every tick" : "",
#else
            g_noevent ? "; no event (NT 4.0 before SP3): asked every tick"
                      : "",
#endif
            g_swpointer ? "; GDI draws the pointer into the picture"
                        : "");
#ifdef NTRDPS_TLS
    g_nt5 = (GetVersion() & 0x800000FFUL) == 5;
    g_fitw = g_fith = g_fitb = 0;
    if (!fit_mirror()) {
        mirror_close();
        return 0;
    }
#endif
    return 1;
}

HANDLE mirror_event(void)
{
    return g_event;
}

int mirror_noevent(void)
{
    return g_event != NULL && g_noevent;
}

int mirror_swpointer(void)
{
    return g_event != NULL && g_swpointer;
}

#ifdef NTRDPS_TLS
void mirror_fit(void)
{
    if (g_event != NULL && !fit_mirror()) {
        mirror_close();
    }
}
#endif

int mirror_layered(void)
{
    return g_event != NULL && g_layered;
}

int mirror_take(RECT *r, int max, unsigned long *flags)
{
    ULONG i, n;

    *flags = 0;
    if (g_event == NULL) {
        return -1;
    }
    if (!escape(MR_CMD_GET, 0) || g_out.status != MR_OK) {
        log_msg(LOG_WARN, "mirror: GET failed (answer %s, status %lu); the "
                "channel is closed, polling",
                g_out.magic == MR_MAGIC ? "ok" : "none",
                (unsigned long) g_out.status);
        mirror_close();
        return -1;
    }
    copy_counts();
#ifdef NTRDPS_TLS
    g_swpointer = (g_out.diag & MR_D_SWPOINTER) != 0;
    if ((g_out.flags & MR_F_MODE) != 0 && !fit_mirror()) {
        mirror_close();
        return -1;
    }
#endif
    *flags = g_out.flags;
    n = g_out.nrects;
    if (n > MR_RECTS) {
        n = MR_RECTS;
    }
    if (max < 0) {
        max = 0;
    }
    if (n > (ULONG) max) {
        n = (ULONG) max;
        *flags |= MR_F_ALL;
    }
    for (i = 0; i < n; i++) {
        r[i].left = g_out.rect[i].left;
        r[i].top = g_out.rect[i].top;
        r[i].right = g_out.rect[i].right;
        r[i].bottom = g_out.rect[i].bottom;
    }
    g_st.gets++;
    g_st.rects += n;
    if (*flags & MR_F_ALL) {
        g_st.alls++;
    }
    return (int) n;
}

static int bitmap_valid(const MR_ORDER *o, const MR_FLUSHOUT *f, LONG w,
                        LONG h)
{
    const MR_BITMAP *b;
    ULONG bpp = f->head.bpp, rop = o->rop;

    if (bpp < 8 || rop > 0xFF || ((rop >> 2) & 0x33) == (rop & 0x33) ||
        ((rop >> 4) & 0x0F) != (rop & 0x0F) || (o->first & 3) != 0 ||
        f->bitmaplen < sizeof(MR_BITMAP) ||
        o->first > f->bitmaplen - sizeof(MR_BITMAP)) {
        return 0;
    }
    b = (const MR_BITMAP *) (mirror_flush_bitmaps(f) + o->first);
    if (b->cx < 1 || b->cx > MR_BITMAP_MAX || b->cy < 1 ||
        b->cy > MR_BITMAP_MAX ||
        MR_BITMAP_SIZE(b->cx, b->cy, bpp) > f->bitmaplen - o->first) {
        return 0;
    }
    return o->sx >= 0 && o->sy >= 0 && (ULONG) (o->sx + w) <= b->cx &&
           (ULONG) (o->sy + h) <= b->cy;
}

static int order_valid(const MR_ORDER *o, const MR_FLUSHOUT *f)
{
    const MR_OUT *h = &f->head;
    LONG w = o->right - o->left, ht = o->bottom - o->top;

    if (o->left < 0 || o->top < 0 || w <= 0 || ht <= 0 ||
        o->right > h->cx || o->bottom > h->cy) {
        return 0;
    }
    switch (o->kind) {
    case MR_O_FILL:
        return o->rop <= 0xFF && ((o->rop >> 2) & 0x33) == (o->rop & 0x33);
    case MR_O_COPY:
        return o->sx >= 0 && o->sy >= 0 && o->sx <= h->cx - w &&
               o->sy <= h->cy - ht;
    case MR_O_LINE:
        return o->rop >= 1 && o->rop <= 16 && (w == 1 || ht == 1);
    case MR_O_TEXT:
        return o->nglyph >= 1 && o->nglyph <= MR_TEXT_MAX &&
               o->first <= f->nplaces && o->nglyph <= f->nplaces - o->first;
    case MR_O_SAVE:
    case MR_O_RESTORE:
        return o->first != 0;
    case MR_O_BITMAP:
        return bitmap_valid(o, f, w, ht);
    case MR_O_PATTERN:
        return o->rop <= 0xFF && ((o->rop >> 2) & 0x33) == (o->rop & 0x33) &&
               ((o->rop >> 4) & 0x0F) != (o->rop & 0x0F) &&
               o->sx >= 0 && o->sx <= 7 && o->sy >= 0 && o->sy <= 7;
    }
    return 0;
}

static int glyphs_valid(const MR_FLUSHOUT *f)
{
    static unsigned char start[MR_GLYPHAREA / 32];
    const unsigned char *a = mirror_flush_glyphs(f), *bits;
    const MR_GLYPHPOS *pos = mirror_flush_places(f);
    const MR_GLYPH *g;
    ULONG at = 0, i, rowb, y, size;

    if (f->glyphlen > MR_GLYPHAREA || (f->glyphlen & 3) != 0) {
        return 0;
    }
    memset(start, 0, sizeof(start));
    while (at < f->glyphlen) {
        if (f->glyphlen - at < sizeof(MR_GLYPH)) {
            return 0;
        }
        g = (const MR_GLYPH *) (a + at);
        rowb = MR_GLYPH_ROWB(g->cx);
        if (g->cx < 1 || g->cy < 1 || g->cx > 8 * MR_GLYPH_BITS ||
            g->cy > MR_GLYPH_BITS || rowb * g->cy > MR_GLYPH_BITS) {
            return 0;
        }
        size = MR_GLYPH_SIZE(g->cx, g->cy);
        if (size > f->glyphlen - at) {
            return 0;
        }
        bits = (const unsigned char *) (g + 1);
        if (g->cx % 8 != 0) {
            for (y = 0; y < g->cy; y++) {
                if (bits[y * rowb + rowb - 1] & (0xFF >> (g->cx % 8))) {
                    return 0;
                }
            }
        }
        start[at / 32] |= (unsigned char) (1 << (at / 4 % 8));
        at += size;
    }
    for (i = 0; i < f->nplaces; i++) {
        at = pos[i].glyph;
        if (at >= f->glyphlen || (at & 3) != 0 ||
            !(start[at / 32] & (1 << (at / 4 % 8)))) {
            return 0;
        }
    }
    return 1;
}

static int flush_valid(const MR_FLUSHOUT *f, unsigned long size)
{
    const MR_OUT *h = &f->head;
    const MR_RECT *r;
    const MR_ORDER *o = mirror_flush_orders(f);
    ULONG i, need = 0, rb;

    if (h->nrects > MR_RECTS || h->cx <= 0 || h->cy <= 0 ||
        (h->bpp != 4 && h->bpp != 8 && h->bpp != 16 && h->bpp != 24 &&
         h->bpp != 32) || f->norders > MR_ORDERS ||
        f->nplaces > MR_PLACES || f->glyphlen > MR_GLYPHAREA ||
        f->bitmaplen > MR_BITMAPAREA || (f->bitmaplen & 3) != 0 ||
        f->datalen > size - sizeof(MR_FLUSHOUT) -
                     f->norders * sizeof(MR_ORDER) -
                     f->nplaces * sizeof(MR_GLYPHPOS) - f->glyphlen -
                     f->bitmaplen) {
        return 0;
    }
    for (i = 0; i < f->norders; i++) {
        if (!order_valid(&o[i], f)) {
            return 0;
        }
    }
    if ((f->nplaces > 0 || f->glyphlen > 0) && !glyphs_valid(f)) {
        return 0;
    }
    for (i = 0; i < h->nrects; i++) {
        r = &h->rect[i];
        if (r->left < 0 || r->top < 0 || r->left >= r->right ||
            r->top >= r->bottom || r->bottom > h->cy ||
            r->right > h->cx + (h->bpp == 4 ? 1 : 0)) {
            return 0;
        }
        rb = MR_ROW_BYTES(r->left, r->right, h->bpp);
        if (h->bpp == 4 && ((r->left | r->right) & 1) != 0) {
            return 0;
        }
        need += rb * (ULONG) (r->bottom - r->top);
        if (need > f->datalen) {
            return 0;
        }
    }
    return need == f->datalen;
}

int mirror_flush(MR_FLUSHOUT *f, unsigned long size, unsigned long fl)
{
    MR_IN in;
    int rc, i;

    if (g_dc == NULL || g_event == NULL) {
        return -1;
    }
    if (size < MR_FLUSH_MIN) {
        return 0;
    }
    in.magic = MR_MAGIC;
    in.version = MR_VERSION;
    in.pid = GetCurrentProcessId();
    in.cmd = MR_CMD_FLUSH;
    in.arg = fl & (MR_FL_ORDERS | MR_FL_TEXT | MR_FL_BITMAPS |
                   MR_FL_PATTERNS);
    memset(f, 0, sizeof(*f));
    rc = ExtEscape(g_dc, MR_ESCAPE, sizeof(in), (LPCSTR) &in, (int) size,
                   (LPSTR) f);
    if (rc == 0 || f->head.magic != MR_MAGIC ||
        f->head.version != MR_VERSION) {
        log_msg(LOG_WARN, "mirror: FLUSH got no answer; the channel is "
                "closed, polling");
        mirror_close();
        return -1;
    }
    if (f->head.status == MR_BADCMD || f->head.status == MR_NOSHADOW) {
        log_msg(LOG_INFO, "mirror: the driver gives no pixels (%s); its "
                "rectangles are read with BitBlt",
                f->head.status == MR_NOSHADOW ? "no memory for its shadow"
                : "an ntrdpsmr.dll from before task M5.2? ntrdps "
                  "-installmirror, then reboot");
        return 0;
    }
    if (f->head.status != MR_OK) {
        log_msg(LOG_WARN, "mirror: FLUSH failed (status %lu); the channel "
                "is closed, polling", (unsigned long) f->head.status);
        mirror_close();
        return -1;
    }
    if (!flush_valid(f, size)) {
        log_msg(LOG_WARN, "mirror: a FLUSH answer does not check out (%lu "
                "orders, %lu rectangles, %lu bytes); from now on its "
                "rectangles are read with BitBlt",
                (unsigned long) f->norders, (unsigned long) f->head.nrects,
                (unsigned long) f->datalen);
        return 0;
    }
    g_out = f->head;
    copy_counts();
#ifdef NTRDPS_TLS
    g_swpointer = (g_out.diag & MR_D_SWPOINTER) != 0;
    if ((f->head.flags & MR_F_MODE) != 0 && !fit_mirror()) {
        mirror_close();
        return -1;
    }
#endif
    g_st.skipped = f->skipped;
    g_st.saves = f->saves;
    g_st.savefail = f->savefail;
    g_st.ordcalls = f->ordcalls;
    g_st.ordmiss = f->ordmiss;
    g_st.textcalls = f->textcalls;
    g_st.textmiss = f->textmiss;
    g_st.saveords = f->saveords;
    g_st.restords = f->restords;
    g_st.bmpcalls = f->bmpcalls;
    g_st.patcalls = f->patcalls;
    memcpy(g_st.textwhy, f->textwhy, sizeof(g_st.textwhy));
    for (i = 0; i < MR_RW_N; i++) {
        if (g_st.flushes == 0) {
            g_rwbase[i] = f->rectwhy[i];
            g_rpbase[i] = f->rectpix[i];
        }
        g_st.rectwhy[i] = f->rectwhy[i] - g_rwbase[i];
        g_st.rectpix[i] = f->rectpix[i] - g_rpbase[i];
    }
    g_st.places += f->nplaces;
    g_st.glyphbytes += f->glyphlen;
    g_st.bitmapbytes += f->bitmaplen;
    g_st.orders += f->norders;
    g_st.flushes++;
    g_st.rects += f->head.nrects;
    g_st.bytes += f->datalen;
    if (f->head.flags & MR_F_ALL) {
        g_st.alls++;
    }
    return 1;
}

void mirror_stats(MIRROR_STATS *s)
{
    *s = g_st;
}

int mirror_pointer(MR_PTROUT *s)
{
    MR_IN in;
    int rc;

    if (g_dc == NULL || g_event == NULL) {
        return 0;
    }
    in.magic = MR_MAGIC;
    in.version = MR_VERSION;
    in.pid = GetCurrentProcessId();
    in.cmd = MR_CMD_POINTER;
    in.arg = 0;
    memset(s, 0, sizeof(*s));
    rc = ExtEscape(g_dc, MR_ESCAPE, sizeof(in), (LPCSTR) &in, sizeof(*s),
                   (LPSTR) s);
    return rc != 0 && s->magic == MR_MAGIC && s->version == MR_VERSION &&
           s->status == MR_OK;
}

void mirror_close(void)
{
    if (g_dc != NULL && g_event != NULL) {
        if (!escape(MR_CMD_STOP, 0) || g_out.status != MR_OK) {
            log_msg(LOG_WARN, "mirror: STOP got no answer (status %lu)",
                    (unsigned long) g_out.status);
        }
    }
    close_handles();
}
