/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "core.h"
#include "desktop.h"
#include "hookdll.h"
#include "cursor.h"

#define OTHER_MS      200
#define SIDE_MAX      256

#define TOLD_NOTHING 0
#define TOLD_ARROW   1
#define TOLD_HANDLE  2
#define TOLD_MIRROR  3

typedef struct {
    BITMAPINFOHEADER h;
    RGBQUAD          c[2];
} MASKINFO;

typedef struct {
    POINT        pos;
    int          have_pos;
    int          told;
    HCURSOR      told_handle;
    int          other;
    DWORD        other_ms;
    int          read_error;
    int          mirror;
} CURSOR;

static CURSOR g_cur;

void cursor_reset(void)
{
    memset(&g_cur, 0, sizeof(g_cur));
}

static void read_failed(CURSOR *c, HCURSOR h, const char *what)
{
    if (!c->read_error) {
        c->read_error = 1;
        log_msg(LOG_WARN, "cursor: the shape of cursor 0x%08lx: %s failed, "
                "error %lu; clients show their default arrow. Logged once.",
                (unsigned long) h, what, (unsigned long) GetLastError());
    }
}

static int read_shape(CURSOR *c, HDC screen, HCURSOR h, CORE_POINTER *p)
{
    ICONINFO ii;
    BITMAP bm;
    MASKINFO mi;
    BITMAPINFOHEADER ci;
    unsigned char *mask = NULL, *color = NULL, *d, bit;
    const unsigned char *m_and, *m_xor, *crow;
    int mono, cx, cy, rows, inv, x, y, ok = 0;
    long ms, cs;

    if (!GetIconInfo(h, &ii)) {
        read_failed(c, h, "GetIconInfo");
        return 0;
    }
    mono = ii.hbmColor == NULL;
    if (GetObjectA(ii.hbmMask, sizeof(bm), &bm) == 0) {
        read_failed(c, h, "GetObject");
        goto done;
    }
    cx = bm.bmWidth;
    cy = mono ? bm.bmHeight / 2 : bm.bmHeight;
    rows = mono ? 2 * cy : cy;
    if (cx <= 0 || cy <= 0 || cx > SIDE_MAX || cy > SIDE_MAX ||
        ii.xHotspot >= CORE_POINTER_MAX || ii.yHotspot >= CORE_POINTER_MAX) {
        log_msg(LOG_DEBUG, "cursor: 0x%08lx is %d x %d, hot spot (%lu, %lu): "
                "the default arrow (rdp-4 9.1)", (unsigned long) h, cx, cy,
                (unsigned long) ii.xHotspot, (unsigned long) ii.yHotspot);
        goto done;
    }
    ms = (long) ((cx + 31) / 32) * 4;
    cs = (long) ((cx * 3 + 3) / 4) * 4;
    mask = (unsigned char *) malloc((size_t) ms * rows);
    color = mono ? NULL : (unsigned char *) malloc((size_t) cs * cy);
    if (mask == NULL || (!mono && color == NULL)) {
        read_failed(c, h, "malloc");
        goto done;
    }

    memset(&mi, 0, sizeof(mi));
    mi.h.biSize = sizeof(mi.h);
    mi.h.biWidth = cx;
    mi.h.biHeight = rows;
    mi.h.biPlanes = 1;
    mi.h.biBitCount = 1;
    mi.h.biCompression = BI_RGB;
    if (GetDIBits(screen, ii.hbmMask, 0, (UINT) rows, mask,
                  (BITMAPINFO *) &mi, DIB_RGB_COLORS) != rows) {
        read_failed(c, h, "GetDIBits of the mask");
        goto done;
    }
    inv = mi.c[0].rgbRed + mi.c[0].rgbGreen + mi.c[0].rgbBlue >
          mi.c[1].rgbRed + mi.c[1].rgbGreen + mi.c[1].rgbBlue;
    if (!mono) {
        memset(&ci, 0, sizeof(ci));
        ci.biSize = sizeof(ci);
        ci.biWidth = cx;
        ci.biHeight = cy;
        ci.biPlanes = 1;
        ci.biBitCount = 24;
        ci.biCompression = BI_RGB;
        if (GetDIBits(screen, ii.hbmColor, 0, (UINT) cy, color,
                      (BITMAPINFO *) &ci, DIB_RGB_COLORS) != cy) {
            read_failed(c, h, "GetDIBits of the colors");
            goto done;
        }
    }

    memset(p->and_mask, 0xFF, sizeof(p->and_mask));
    memset(p->color, 0, sizeof(p->color));
    p->width = (unsigned int) (cx < CORE_POINTER_MAX ? cx : CORE_POINTER_MAX);
    p->height = (unsigned int) (cy < CORE_POINTER_MAX ? cy : CORE_POINTER_MAX);
    p->hot_x = (unsigned int) ii.xHotspot;
    p->hot_y = (unsigned int) ii.yHotspot;
    for (y = 0; y < (int) p->height; y++) {
        m_and = mask + (long) (mono ? 2 * cy - 1 - y : cy - 1 - y) * ms;
        m_xor = mask + (long) (cy - 1 - y) * ms;
        crow = mono ? NULL : color + (long) (cy - 1 - y) * cs;
        for (x = 0; x < (int) p->width; x++) {
            bit = (unsigned char) (0x80 >> (x & 7));
            if (((m_and[x >> 3] & bit) != 0) == inv) {
                p->and_mask[y][x >> 3] &= (unsigned char) ~bit;
            }
            d = &p->color[y][x * 3];
            if (crow != NULL) {
                d[0] = crow[x * 3 + 2];
                d[1] = crow[x * 3 + 1];
                d[2] = crow[x * 3];
            } else if (((m_xor[x >> 3] & bit) != 0) != inv) {
                d[0] = d[1] = d[2] = 0xFF;
            }
        }
    }
    ok = 1;
    log_msg(LOG_DEBUG, "cursor: 0x%08lx: %s, %d x %d, hot spot (%u, %u)",
            (unsigned long) h, mono ? "mono" : "color", cx, cy, p->hot_x,
            p->hot_y);
done:
    free(mask);
    free(color);
    DeleteObject(ii.hbmMask);
    if (ii.hbmColor != NULL) {
        DeleteObject(ii.hbmColor);
    }
    return ok;
}

static void tell_arrow(CURSOR *c)
{
    if (c->told != TOLD_ARROW) {
        c->told = TOLD_ARROW;
        core_pointer_shape(NULL);
    }
}

static void tell_handle(CURSOR *c, HDC screen, HCURSOR h)
{
    CORE_POINTER p;

    if (c->told == TOLD_HANDLE && c->told_handle == h) {
        return;
    }
    c->told = TOLD_HANDLE;
    c->told_handle = h;
    memset(&p, 0, sizeof(p));
    p.cb = sizeof(p);
    if (h == NULL) {
        log_msg(LOG_DEBUG, "cursor: hidden (no cursor)");
        core_pointer_shape(&p);
    } else if (read_shape(c, screen, h, &p)) {
        core_pointer_shape(&p);
    } else {
        core_pointer_shape(NULL);
    }
}

static int button_held(void)
{
    return ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) |
             GetAsyncKeyState(VK_MBUTTON)) & 0x8000) != 0;
}

static int other_window(CURSOR *c, HWND w, DWORD thread)
{
    DWORD pid, tid, now;

    tid = w != NULL ? GetWindowThreadProcessId(w, &pid) : 0;
    if (w == NULL || (tid != 0 && tid == thread) || button_held()) {
        c->other = 0;
        return 0;
    }
    now = GetTickCount();
    if (c->other == 0) {
        c->other = 1;
        c->other_ms = now;
    }
    if (now - c->other_ms < OTHER_MS) {
        return 0;
    }
    if (c->other == 1) {
        c->other = 2;
        log_msg(LOG_DEBUG, "cursor: the hook does not see thread %lu (0: a "
                "system thread) of the window under the pointer: a guess",
                (unsigned long) tid);
    }
    return 1;
}

static HCURSOR guess_cursor(HWND w, POINT pt)
{
    RECT wr, cr;
    POINT cpt;
    HWND child;
    HCURSOR h = NULL;

    if (w == NULL) {
        return LoadCursorA(NULL, IDC_ARROW);
    }
    if ((GetWindowLongA(w, GWL_STYLE) & WS_THICKFRAME) && GetWindowRect(w, &wr)) {
        int cx = GetSystemMetrics(SM_CXFRAME);
        int cy = GetSystemMetrics(SM_CYFRAME);
        int l = pt.x <  wr.left   + cx;
        int r = pt.x >= wr.right  - cx;
        int t = pt.y <  wr.top    + cy;
        int b = pt.y >= wr.bottom - cy;
        LPCSTR id = NULL;
        const char *nm = NULL;
        if ((l && t) || (r && b))      { id = IDC_SIZENWSE; nm = "size-nwse"; }
        else if ((r && t) || (l && b)) { id = IDC_SIZENESW; nm = "size-nesw"; }
        else if (l || r)               { id = IDC_SIZEWE;   nm = "size-we"; }
        else if (t || b)               { id = IDC_SIZENS;   nm = "size-ns"; }
        if (id != NULL) {
            log_msg(LOG_DEBUG, "cursor: %s over window 0x%08lx border", nm,
                    (unsigned long) w);
            return LoadCursorA(NULL, id);
        }
    }
    cpt = pt;
    if (GetClientRect(w, &cr) && ScreenToClient(w, &cpt) && PtInRect(&cr, cpt)) {
        child = ChildWindowFromPoint(w, cpt);
        h = (HCURSOR) GetClassLongA(child != NULL ? child : w, GCL_HCURSOR);
    }
    return h != NULL ? h : LoadCursorA(NULL, IDC_ARROW);
}

void cursor_check(HDC screen, const char *desktop)
{
    CURSOR *c = &g_cur;
    POINT pt;
    HWND w;
    HCURSOR h;
    LONG serial;
    DWORD thread;

    if (GetCursorPos(&pt) &&
        (!c->have_pos || pt.x != c->pos.x || pt.y != c->pos.y)) {
        c->pos = pt;
        c->have_pos = 1;
        core_pointer_pos((int) pt.x, (int) pt.y);
    }
    if (c->mirror) {
        return;
    }
    if (!hookdll_ready() ||
        (desktop[0] != '\0' && lstrcmpiA(desktop, "Default") != 0)) {
        tell_arrow(c);
        return;
    }
    if (screen == NULL) {
        return;
    }
    h = hookdll_cursor(&serial, &thread);
    w = c->have_pos ? WindowFromPoint(c->pos) : NULL;
    if (serial == 0 || desktop_nt31() || other_window(c, w, thread)) {
        h = guess_cursor(w, c->pos);
    }
    tell_handle(c, screen, h);
}

void cursor_mirror(const CORE_POINTER *p)
{
    CURSOR *c = &g_cur;

    if (p == NULL) {
        if (c->mirror) {
            c->mirror = 0;
            c->told = TOLD_NOTHING;
        }
        return;
    }
    c->mirror = 1;
    c->told = TOLD_MIRROR;
    core_pointer_shape(p);
}
