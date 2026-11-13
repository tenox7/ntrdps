/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "core.h"
#include "desktop.h"
#include "palette.h"
#include "cursor.h"
#include "hookdll.h"
#include "mrchan.h"
#include "mirror.h"
#include "mrshape.h"
#include "draw.h"
#include "reader.h"

#define TILE          32
#define TICK_MS       25
#define FOLLOW_TICKS  10
#define PALETTE_TICKS 20
#define READY_WAIT_MS 5000
#define STOP_WAIT_MS  5000
#define HINT_TAKE     64
#define WARM_TICKS    4
#define WARM_LINES    4
#define HOT_TILES     12
#define SLOW_TICKS    4
#define CONSOLE_TICKS 10
#define CONSOLE_MAX   16
#define CONSOLE_CLASS "ConsoleWindowClass"
#define CPU_MS        60000
#define MODE_TICKS    40
#define MR_GAP_MS     10
#define MR_SETTLE_MS  10
#define SAFETY_MS     1000
#define SAFETY_QUIET  200
#define SAFETY_MAX_MS 5000
#define FLUSH_SMALL   131072UL
#define FLUSH_BIG     262144UL
#define FLUSH_PASSES  64
#define FLUSH_MISFIT  10
#define SHADOW_TICKS  200

typedef char flush_small_fits[MR_FLUSH_MIN <= FLUSH_SMALL ? 1 : -1];

typedef HBITMAP (WINAPI *CREATEDIBSECTION)(HDC, const BITMAPINFO *, UINT,
                                           void **, HANDLE, DWORD);

typedef struct {
    BITMAPINFOHEADER h;
    RGBQUAD          c[256];
} DIBINFO;

#define SAVE_COPIES 16

typedef struct {
    unsigned long  num;
    RECT           r;
    unsigned char *bits;
    unsigned long  age;
} SAVE_COPY;

typedef struct {
    int            w, h;
    int            depth;
    unsigned long  green_mask;
    unsigned long  red_mask;
    unsigned long  blue_mask;
    int            flush_ok;
    int            bytes;
    int            cap_bpp;
    long           stride;
    long           cap_stride;
    int            cols, rows;
    unsigned char *bits;
    unsigned char  palette[256][3];
    int            palette_device;
    DIBINFO        dib;
    int            getdibits;

    HANDLE         thread, stop, ready;
    HDC            mem;
    HBITMAP        bm, old_bm;
    HDC            paldc;
    HPALETTE       pal, old_pal;
    int            shared;
    int            rd;
    unsigned char *cap;
    unsigned char *cand;
    RECT          *dirty;
    int           *lines;
    int            reads;
    int            lost;
    int            broken;
    int            read_error;
    int            denied;
    int            follow_now;
    unsigned long  t_calls;
    unsigned long  t_rows;
    unsigned char *warm;
    unsigned char *hot;
    RECT           consoles[CONSOLE_MAX];
    int            nconsoles;
    unsigned long  hint_rects;
    unsigned long  hint_lost;
    double         cpu_s;
    DWORD          cpu_ms;
    double         work_ms;
    int            mirror;
    DWORD          settle_ms;
    unsigned long  mr_ask;
    unsigned long  mr_passes;
    unsigned long  mr_rects;
    unsigned long  mr_safety;
    unsigned long  mr_draw_ms;
    unsigned long  safety_ms;
    unsigned int   safety_n;
    int            mr_noshape;
    MR_PTROUT      mr_shape;
    int            mr_flush;
    int            mr_more;
    int            mr_misfit;
    unsigned char *mr_buf;
    unsigned long  mr_last;
    unsigned long  mr_flushes;
    unsigned long  mr_bytes;
    unsigned long  mr_orders;
    unsigned long  mr_ordskip;
    int            ord_asked;
    int            ord_pause;
    unsigned long  mr_pauses;
    unsigned long  mr_resumes;
    unsigned long  mr_skipped;
    unsigned long  sc_rounds;
    unsigned long  sc_busy;
    unsigned long  sc_bad;
    SAVE_COPY      sv[SAVE_COPIES];
    unsigned long  sv_age;
    unsigned long  sv_made;
    unsigned long  sv_restored;
    unsigned long  sv_freed;
    unsigned long  sv_missing;
    unsigned long  sv_nomem;
    unsigned long  bm_orders;
    unsigned long  bm_nomem;
    unsigned long  pt_pieces;
    unsigned long  pt_plain;
    unsigned long  pt_copies;
    unsigned long  pt_rgb8;
    unsigned long  pt_other;
} POLL;

static POLL *g_poll;
static int   g_getdibits;
static int   g_nohints;
static int   g_nomirror;
static int   g_shadowcheck;
static int   g_noorders;
static int   g_notext;
static int   g_nobitmaps;
static int   g_nopatterns;
static volatile LONG g_input_hint;

static CORE_GLYPH    g_tglyph[MR_TEXT_MAX];
static CORE_GLYPHREF g_tref[MR_TEXT_MAX];
static unsigned long g_trun[(MR_TEXT_MAX + CORE_TEXT_MAX - 1) /
                            CORE_TEXT_MAX];
static int           g_tlogged;

static unsigned char g_bpix[CORE_BITMAP_MAX * CORE_BITMAP_MAX * 3];
static CORE_BITMAP   g_bmp;
static unsigned long g_boff;
static int           g_bvalid;
static int           g_bid;
static unsigned long g_bserial;

void poll_set_shadowcheck(int on)
{
    g_shadowcheck = on;
}

void poll_set_noorders(int on)
{
    g_noorders = on;
}

void poll_set_notext(int on)
{
    g_notext = on;
}

void poll_set_nobitmaps(int on)
{
    g_nobitmaps = on;
}

void poll_set_nopatterns(int on)
{
    g_nopatterns = on;
}

void poll_set_getdibits(int on)
{
    g_getdibits = on;
}

void poll_set_nomirror(int on)
{
    g_nomirror = on;
}

void poll_set_nohints(int on)
{
    g_nohints = on;
}

void poll_input_hint(void)
{
    InterlockedExchange((LONG *) &g_input_hint, 1);
}

static int dib_colors(HDC dc, int n, unsigned char *out)
{
    DIBINFO bi;
    unsigned char px[4];
    HBITMAP bm;
    int i, ok;

    bm = CreateCompatibleBitmap(dc, 1, 1);
    if (bm == NULL) {
        return 0;
    }
    memset(&bi, 0, sizeof(bi));
    bi.h.biSize = sizeof(bi.h);
    bi.h.biWidth = 1;
    bi.h.biHeight = 1;
    bi.h.biPlanes = 1;
    bi.h.biBitCount = (WORD) (n == 16 ? 4 : 8);
    bi.h.biCompression = BI_RGB;
    ok = GetDIBits(dc, bm, 0, 1, px, (BITMAPINFO *) &bi,
                   DIB_RGB_COLORS) == 1;
    DeleteObject(bm);
    for (i = 0; ok && i < n; i++) {
        out[i * 3] = bi.c[i].rgbRed;
        out[i * 3 + 1] = bi.c[i].rgbGreen;
        out[i * 3 + 2] = bi.c[i].rgbBlue;
    }
    return ok;
}

static int probe_pixels(HDC dc, unsigned long v[3])
{
    static const COLORREF probe[3] = {
        RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255)
    };
    unsigned char px[4];
    HDC mem;
    HBITMAP bm, old;
    int i, ok = 1;

    mem = CreateCompatibleDC(dc);
    bm = CreateCompatibleBitmap(dc, 1, 1);
    for (i = 0; i < 3 && mem != NULL && bm != NULL; i++) {
        old = (HBITMAP) SelectObject(mem, bm);
        SetPixel(mem, 0, 0, probe[i]);
        GdiFlush();
        SelectObject(mem, old);
        memset(px, 0, sizeof(px));
        if (GetBitmapBits(bm, sizeof(px), px) < 2) {
            ok = 0;
        }
        v[i] = px[0] | ((unsigned long) px[1] << 8) |
               ((unsigned long) px[2] << 16);
    }
    if (bm != NULL) {
        DeleteObject(bm);
    }
    if (mem != NULL) {
        DeleteDC(mem);
    }
    return mem != NULL && bm != NULL && ok;
}

static int probe_masks(HDC dc, CORE_FRAME *f)
{
    unsigned long mask[3];
    int i;

    if (!probe_pixels(dc, mask)) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        mask[i] &= 0xFFFF;
    }
    if (mask[0] == 0 || mask[1] == 0 || mask[2] == 0 ||
        (mask[0] & mask[1]) != 0 || (mask[1] & mask[2]) != 0 ||
        (mask[0] & mask[2]) != 0) {
        return 0;
    }
    f->red_mask = mask[0];
    f->green_mask = mask[1];
    f->blue_mask = mask[2];
    return 1;
}

static int probe_bgr(HDC dc)
{
    unsigned long v[3];

    return probe_pixels(dc, v) && v[0] == 0xFF0000UL &&
           v[1] == 0x00FF00UL && v[2] == 0x0000FFUL;
}

static void palette_256(POLL *p, HDC dc, int palette_device)
{
    PALETTEENTRY pe[256];
    int i;

    if (palette_device && GetSystemPaletteEntries(dc, 0, 256, pe) == 256) {
        for (i = 0; i < 256; i++) {
            p->palette[i][0] = pe[i].peRed;
            p->palette[i][1] = pe[i].peGreen;
            p->palette[i][2] = pe[i].peBlue;
        }
        return;
    }
    if (!dib_colors(dc, 256, &p->palette[0][0])) {
        log_msg(LOG_WARN, "poll: no 8 bpp palette from GDI; the 16 VGA "
                "colors of rdp-4 1.4 are used");
        palette_16(p->palette, &vga_colors[0][0]);
    }
}

static int read_format(POLL *p, CORE_FRAME *f)
{
    unsigned char colors[16][3];
    int bitspixel, planes, palette_device;
    HDC dc;

    dc = CreateDCA("DISPLAY", NULL, NULL, NULL);
    if (dc == NULL) {
        log_msg(LOG_ERROR, "poll: CreateDC(DISPLAY) failed, error %lu",
                (unsigned long) GetLastError());
        return 0;
    }
    p->w = GetDeviceCaps(dc, HORZRES);
    p->h = GetDeviceCaps(dc, VERTRES);
    bitspixel = GetDeviceCaps(dc, BITSPIXEL);
    planes = GetDeviceCaps(dc, PLANES);
    p->depth = bitspixel * planes;
    palette_device = (GetDeviceCaps(dc, RASTERCAPS) & RC_PALETTE) != 0;
    log_msg(LOG_INFO, "poll: console %d x %d, %d bpp (bitspixel %d, planes "
            "%d, palette %s)", p->w, p->h, p->depth, bitspixel, planes,
            palette_device ? "yes" : "no");

    p->flush_ok = 1;
    if (p->depth == 4) {
        f->bpp = 8;
        p->cap_bpp = 4;
        if (!dib_colors(dc, 16, &colors[0][0])) {
            log_msg(LOG_WARN, "poll: no 16 console colors from GDI; the "
                    "VGA colors of rdp-4 1.4 are used");
            memcpy(colors, vga_colors, sizeof(colors));
        }
        palette_16(p->palette, &colors[0][0]);
    } else if (p->depth == 8) {
        f->bpp = 8;
        p->cap_bpp = 8;
        palette_256(p, dc, palette_device);
        p->palette_device = palette_device;
    } else if (p->depth == 15 || p->depth == 16) {
        f->bpp = 16;
        p->cap_bpp = 16;
        if (!probe_masks(dc, f)) {
            log_msg(LOG_WARN, "poll: the color masks are unknown; 565 is "
                    "used");
            f->red_mask = 0xF800;
            f->green_mask = 0x07E0;
            f->blue_mask = 0x001F;
        }
        p->green_mask = f->green_mask;
        p->red_mask = f->red_mask;
        p->blue_mask = f->blue_mask;
    } else if (p->depth == 24 || p->depth == 32) {
        f->bpp = 24;
        p->cap_bpp = 24;
        p->flush_ok = probe_bgr(dc);
    } else {
        log_msg(LOG_ERROR, "poll: a console with %d bpp is not supported",
                p->depth);
        DeleteDC(dc);
        return 0;
    }
    DeleteDC(dc);
    if (p->w <= 0 || p->h <= 0) {
        log_msg(LOG_ERROR, "poll: no console size from GDI");
        return 0;
    }
    p->bytes = (int) f->bpp / 8;
    p->stride = ((long) p->w * f->bpp + 31) / 32 * 4;
    p->cap_stride = ((long) p->w * p->cap_bpp + 31) / 32 * 4;
    return 1;
}

static void dib_palette(POLL *p)
{
    DIBINFO *d = &p->dib;
    int i, n = 1 << p->cap_bpp;

    for (i = 0; i < n; i++) {
        d->c[i].rgbRed = p->palette[i][0];
        d->c[i].rgbGreen = p->palette[i][1];
        d->c[i].rgbBlue = p->palette[i][2];
    }
    d->h.biClrUsed = (DWORD) n;
}

static void dib_format(POLL *p, const CORE_FRAME *f)
{
    DIBINFO *d = &p->dib;
    DWORD *masks = (DWORD *) d->c;

    memset(d, 0, sizeof(*d));
    d->h.biSize = sizeof(d->h);
    d->h.biWidth = p->w;
    d->h.biHeight = p->h;
    d->h.biPlanes = 1;
    d->h.biBitCount = (WORD) p->cap_bpp;
    d->h.biCompression = BI_RGB;
    if (p->cap_bpp <= 8) {
        dib_palette(p);
    } else if (p->cap_bpp == 16) {
        d->h.biCompression = BI_BITFIELDS;
        masks[0] = f->red_mask;
        masks[1] = f->green_mask;
        masks[2] = f->blue_mask;
    }
}

static unsigned char *cap_row(const POLL *p, int y)
{
    return p->cap + (long) (p->h - 1 - y) * p->cap_stride;
}

static unsigned char *frame_row(const POLL *p, int y)
{
    return p->bits + (long) y * p->stride;
}

static void pal_dc(POLL *p, HDC screen)
{
    static struct {
        WORD         ver, n;
        PALETTEENTRY e[256];
    } lp;
    static unsigned char rgb[256][3];
    int i, n;

    if (!p->getdibits || !p->palette_device || p->cap_bpp != 8) {
        return;
    }
    memcpy(rgb, p->palette, sizeof(rgb));
    n = palette_capture(rgb);
    if (n > 0) {
        log_msg(LOG_INFO, "poll: %d palette entries repeat a static color "
                "(NT 3.1 S3: gray 248 = 164): in the palette DC they are "
                "one step off, so the static entries come out", n);
    }
    lp.ver = 0x300;
    lp.n = 256;
    for (i = 0; i < 256; i++) {
        lp.e[i].peRed = rgb[i][0];
        lp.e[i].peGreen = rgb[i][1];
        lp.e[i].peBlue = rgb[i][2];
        lp.e[i].peFlags = 0;
    }
    p->paldc = CreateCompatibleDC(screen);
    p->pal = p->paldc != NULL ? CreatePalette((LOGPALETTE *) &lp) : NULL;
    if (p->pal == NULL) {
        log_msg(LOG_WARN, "poll: no palette DC for GetDIBits, error %lu: "
                "only the 20 static colors come out right",
                (unsigned long) GetLastError());
        if (p->paldc != NULL) {
            DeleteDC(p->paldc);
            p->paldc = NULL;
        }
        return;
    }
    p->old_pal = SelectPalette(p->paldc, p->pal, FALSE);
}

static void pal_dc_free(POLL *p)
{
    if (p->paldc == NULL) {
        return;
    }
    SelectPalette(p->paldc, p->old_pal, FALSE);
    DeleteObject(p->pal);
    DeleteDC(p->paldc);
    p->paldc = NULL;
    p->pal = NULL;
}

static int setup(POLL *p, HDC screen)
{
    CREATEDIBSECTION create = NULL;
    HMODULE gdi;
    void *bits = NULL;
    unsigned long size = (unsigned long) p->cap_stride * p->h;

    if (!p->getdibits) {
        gdi = GetModuleHandleA("GDI32.DLL");
        if (gdi != NULL) {
            create = (CREATEDIBSECTION) GetProcAddress(gdi,
                                                       "CreateDIBSection");
        }
        if (create == NULL) {
            p->getdibits = 1;
        }
    }
    p->mem = CreateCompatibleDC(screen);
    if (p->mem != NULL && p->getdibits) {
        p->bm = CreateCompatibleBitmap(screen, p->w, p->h);
        p->cap = reader_wanted() ? reader_buffer(size) : NULL;
        p->shared = p->cap != NULL;
        if (p->cap == NULL) {
            p->cap = (unsigned char *) malloc((size_t) size);
        }
    } else if (p->mem != NULL) {
        p->bm = create(screen, (BITMAPINFO *) &p->dib, DIB_RGB_COLORS,
                       &bits, NULL, 0);
        p->cap = (unsigned char *) bits;
    }
    if (p->mem == NULL || p->bm == NULL || p->cap == NULL) {
        if (!p->broken) {
            p->broken = 1;
            log_msg(LOG_ERROR, "poll: no capture %s (%d x %d, %d bpp), "
                    "error %lu: the screen stays as it is",
                    p->getdibits ? "bitmap" : "DIB section", p->w, p->h,
                    p->cap_bpp, (unsigned long) GetLastError());
        }
        if (p->getdibits && !p->shared) {
            free(p->cap);
        }
        if (p->bm != NULL) {
            DeleteObject(p->bm);
        }
        if (p->mem != NULL) {
            DeleteDC(p->mem);
        }
        p->cap = NULL;
        p->bm = NULL;
        p->mem = NULL;
        p->shared = 0;
        return 0;
    }
    p->old_bm = (HBITMAP) SelectObject(p->mem, p->bm);
    pal_dc(p, screen);
    if (p->shared) {
        reader_format(p->w, p->h, p->cap_stride, &p->dib, sizeof(p->dib),
                      p->paldc != NULL);
    }
    log_msg(LOG_INFO, "poll: capture with %s, %d bpp DIB%s, tiles %d x %d%s",
            p->getdibits ? "BitBlt + GetDIBits" : "a DIB section",
            p->cap_bpp, p->paldc != NULL ? " (palette DC)" : "",
            p->cols, p->rows, p->shared
            ? "; NT 3.1: other desktops through reader processes" : "");
    return 1;
}

static void teardown(POLL *p)
{
    if (p->mem == NULL) {
        return;
    }
    SelectObject(p->mem, p->old_bm);
    DeleteObject(p->bm);
    DeleteDC(p->mem);
    if (p->getdibits && !p->shared) {
        free(p->cap);
    }
    pal_dc_free(p);
    p->mem = NULL;
    p->bm = NULL;
    p->cap = NULL;
    p->shared = 0;
    p->rd = 0;
}

static int get_rows(POLL *p, HDC screen, int y0, int y1)
{
    DIBINFO d = p->dib;

    return GetDIBits(p->paldc != NULL ? p->paldc : screen, p->bm,
                     (UINT) (p->h - y1), (UINT) (y1 - y0),
                     cap_row(p, y1 - 1), (BITMAPINFO *) &d,
                     DIB_RGB_COLORS) == y1 - y0;
}

static int read_failed(POLL *p, const char *what)
{
    DWORD err = GetLastError();

    if (err == ERROR_ACCESS_DENIED && !p->denied) {
        p->denied = 1;
        p->follow_now = 1;
        log_msg(LOG_DEBUG, "poll: %s failed, error 5: has the input "
                "desktop changed? It is followed at the next tick", what);
        return 0;
    }
    if (!p->read_error) {
        p->read_error = 1;
        log_msg(LOG_WARN, "poll: %s failed, error %lu", what,
                (unsigned long) err);
    }
    return 0;
}

static int read_area(POLL *p, HDC screen, int x0, int y0, int x1, int y1)
{
    int ok;

    p->t_calls++;
    p->t_rows += (unsigned long) (y1 - y0);
    if (p->rd) {
        return reader_area(x0, y0, x1, y1) ? 1 :
               read_failed(p, "a read through the reader process");
    }
    if (!BitBlt(p->mem, x0, y0, x1 - x0, y1 - y0, screen, x0, y0,
                SRCCOPY)) {
        return read_failed(p, "BitBlt");
    }
    if (!p->getdibits) {
        GdiFlush();
        return 1;
    }
    SelectObject(p->mem, p->old_bm);
    ok = get_rows(p, screen, y0, y1);
    SelectObject(p->mem, p->bm);
    return ok ? 1 : read_failed(p, "GetDIBits");
}

static int read_lines(POLL *p, HDC screen, int x0, int x1, int r0, int r1)
{
    int r, ok = 1;

    p->t_calls += (unsigned long) (r1 - r0 + 1);
    p->t_rows += (unsigned long) (r1 - r0 + 1);
    if (p->rd) {
        return reader_lines(x0, x1, &p->lines[r0], r1 - r0 + 1) ? 1 :
               read_failed(p, "a read through the reader process");
    }
    for (r = r0; r <= r1; r++) {
        if (!BitBlt(p->mem, x0, p->lines[r], x1 - x0, 1, screen, x0,
                    p->lines[r], SRCCOPY)) {
            return read_failed(p, "BitBlt");
        }
    }
    if (!p->getdibits) {
        GdiFlush();
        return 1;
    }
    SelectObject(p->mem, p->old_bm);
    for (r = r0; r <= r1 && ok; r++) {
        ok = get_rows(p, screen, p->lines[r], p->lines[r] + 1);
    }
    SelectObject(p->mem, p->bm);
    return ok ? 1 : read_failed(p, "GetDIBits");
}

static const unsigned char *cap_pixels(const POLL *p, int x, int y, int n,
                                       unsigned char *tmp)
{
    const unsigned char *s = cap_row(p, y);
    int i;

    if (p->cap_bpp != 4) {
        return s + (long) x * p->bytes;
    }
    s += x / 2;
    for (i = 0; i < n; i += 2, s++) {
        tmp[i] = (unsigned char) (*s >> 4);
        if (i + 1 < n) {
            tmp[i + 1] = (unsigned char) (*s & 0x0F);
        }
    }
    return tmp;
}

static void frame_to_cap4(POLL *p, const RECT *r)
{
    unsigned char *f, *c;
    int x, y;

    for (y = r->top; y < r->bottom; y++) {
        f = frame_row(p, y);
        c = cap_row(p, y);
        for (x = r->left; x < r->right; x++) {
            f[x] &= 0x0F;
            if (x & 1) {
                c[x / 2] = (unsigned char) ((c[x / 2] & 0xF0) | f[x]);
            } else {
                c[x / 2] = (unsigned char) ((c[x / 2] & 0x0F) | (f[x] << 4));
            }
        }
    }
}

static int tile_width(const POLL *p, int c)
{
    return p->w - c * TILE < TILE ? p->w - c * TILE : TILE;
}

static int tile_height(const POLL *p, int r)
{
    return p->h - r * TILE < TILE ? p->h - r * TILE : TILE;
}

static int check_tile(POLL *p, int c, int r, RECT *rc)
{
    unsigned char tmp[TILE];
    const unsigned char *s;
    unsigned char *d;
    int x = c * TILE, n = tile_width(p, c), nb = n * p->bytes;
    int y, y0 = r * TILE, y1 = y0 + tile_height(p, r), first = -1, last = 0;

    for (y = y0; y < y1; y++) {
        s = cap_pixels(p, x, y, n, tmp);
        d = frame_row(p, y) + (long) x * p->bytes;
        if (memcmp(s, d, nb) != 0) {
            if (first < 0) {
                first = y;
                core_frame_enter();
            }
            memcpy(d, s, nb);
            last = y;
        }
    }
    if (first < 0) {
        return 0;
    }
    core_frame_leave();
    rc->left = x;
    rc->top = first;
    rc->right = x + n;
    rc->bottom = last + 1;
    return 1;
}

static void check(POLL *p, HDC screen, int read)
{
    RECT box;
    int r, c, c0, c1 = 0, n = 0, i;

    for (r = 0; r < p->rows; r++) {
        c0 = -1;
        for (c = 0; c < p->cols; c++) {
            if (p->cand[r * p->cols + c]) {
                if (c0 < 0) {
                    c0 = c;
                }
                c1 = c;
            }
        }
        if (c0 < 0) {
            continue;
        }
        if (read && !read_area(p, screen, c0 * TILE, r * TILE,
                               c1 * TILE + tile_width(p, c1),
                               r * TILE + tile_height(p, r))) {
            memset(p->cand + r * p->cols, 0, (size_t) p->cols);
            continue;
        }
        for (c = c0; c <= c1; c++) {
            if (p->cand[r * p->cols + c]) {
                p->cand[r * p->cols + c] = 0;
                n += check_tile(p, c, r, &p->dirty[n]);
            }
        }
    }
    if (n == 0) {
        return;
    }
    box = p->dirty[0];
    for (i = 1; i < n; i++) {
        if (p->dirty[i].left < box.left) {
            box.left = p->dirty[i].left;
        }
        if (p->dirty[i].top < box.top) {
            box.top = p->dirty[i].top;
        }
        if (p->dirty[i].right > box.right) {
            box.right = p->dirty[i].right;
        }
        if (p->dirty[i].bottom > box.bottom) {
            box.bottom = p->dirty[i].bottom;
        }
    }
    log_msg(LOG_DEBUG, "poll: %d dirty tile%s in (%ld,%ld)-(%ld,%ld)", n,
            n == 1 ? "" : "s", (long) box.left, (long) box.top,
            (long) box.right, (long) box.bottom);
    core_damage(p->dirty, n);
}

static int full_read(POLL *p, HDC screen)
{
    double t = log_ms();
    unsigned long tenths;

    if (!read_area(p, screen, 0, 0, p->w, p->h)) {
        return 0;
    }
    t = log_ms() - t;
    if (t < 0.0) {
        t = 0.0;
    }
    tenths = (unsigned long) (t * 10.0 + 0.5);
    p->read_error = 0;
    p->denied = 0;
    p->reads++;
    log_msg(p->reads == 1 ? LOG_INFO : LOG_DEBUG, "poll: full-screen read "
            "%d: %lu.%lu ms", p->reads, tenths / 10, tenths % 10);
    memset(p->cand, 1, (size_t) p->cols * p->rows);
    check(p, screen, 0);
    return 1;
}

static int sample_line(unsigned int tick)
{
    unsigned int k = tick % TILE, line = 0, i;

    for (i = 0; i < 5; i++) {
        line = (line << 1) | (k & 1);
        k >>= 1;
    }
    return (int) line;
}

static int sample_at(POLL *p, HDC screen, int line, int c0, int c1, int r0,
                     int r1)
{
    unsigned char tmp[TILE];
    const unsigned char *s;
    int r, c, n, any = 0;

    for (r = r0; r <= r1; r++) {
        p->lines[r] = r * TILE + line % tile_height(p, r);
    }
    if (!read_lines(p, screen, c0 * TILE, c1 * TILE + tile_width(p, c1), r0,
                    r1)) {
        return 0;
    }
    p->read_error = 0;
    for (r = r0; r <= r1; r++) {
        for (c = c0; c <= c1; c++) {
            n = tile_width(p, c);
            s = cap_pixels(p, c * TILE, p->lines[r], n, tmp);
            if (memcmp(s, frame_row(p, p->lines[r]) + (long) c * TILE *
                       p->bytes, (size_t) n * p->bytes) != 0) {
                p->cand[r * p->cols + c] = 1;
                any = 1;
            }
        }
    }
    return any;
}

static int sample(POLL *p, HDC screen, unsigned int tick, int c0, int c1,
                  int r0, int r1)
{
    return sample_at(p, screen, sample_line(tick), c0, c1, r0, r1);
}

static int hints_on(const char *desktop)
{
    return !g_nohints && hookdll_ready() &&
           (desktop[0] == '\0' || lstrcmpiA(desktop, "Default") == 0);
}

static int tiles_of(const POLL *p, const RECT *r, int *c0, int *c1, int *r0,
                    int *r1)
{
    int x0 = r->left < 0 ? 0 : (int) r->left;
    int y0 = r->top < 0 ? 0 : (int) r->top;
    int x1 = r->right > p->w ? p->w : (int) r->right;
    int y1 = r->bottom > p->h ? p->h : (int) r->bottom;

    if (x0 >= x1 || y0 >= y1) {
        return 0;
    }
    *c0 = x0 / TILE;
    *c1 = (x1 - 1) / TILE;
    *r0 = y0 / TILE;
    *r1 = (y1 - 1) / TILE;
    return 1;
}

static void take_hints(POLL *p)
{
    RECT r[HINT_TAKE];
    unsigned char *mark;
    int n, i, all, c, c0, c1, row, r0, r1;

    n = hookdll_hints(r, HINT_TAKE, &all);
    p->hint_rects += (unsigned long) n;
    if (all) {
        p->hint_lost++;
        memset(p->warm, WARM_TICKS, (size_t) p->cols * p->rows);
    }
    for (i = 0; i < n; i++) {
        if (tiles_of(p, &r[i], &c0, &c1, &r0, &r1)) {
            mark = (c1 - c0 + 1) * (r1 - r0 + 1) <= HOT_TILES ? p->hot
                                                              : p->warm;
            for (row = r0; row <= r1; row++) {
                for (c = c0; c <= c1; c++) {
                    mark[row * p->cols + c] = WARM_TICKS;
                }
            }
        }
    }
}

static BOOL CALLBACK console_window(HWND w, LPARAM arg)
{
    POLL *p = (POLL *) arg;
    char name[sizeof(CONSOLE_CLASS) + 1];
    RECT r;

    if (p->nconsoles < CONSOLE_MAX && IsWindowVisible(w) && !IsIconic(w) &&
        GetClassNameA(w, name, sizeof(name)) > 0 &&
        lstrcmpiA(name, CONSOLE_CLASS) == 0 && GetWindowRect(w, &r)) {
        p->consoles[p->nconsoles++] = r;
    }
    return TRUE;
}

static void find_consoles(POLL *p)
{
    p->nconsoles = 0;
    EnumWindows(console_window, (LPARAM) p);
}

static void warm_console_focus(POLL *p)
{
    char name[sizeof(CONSOLE_CLASS) + 1];
    HWND w = GetForegroundWindow();
    RECT r;
    int c, c0, c1, row, r0, r1;

    if (w == NULL || GetClassNameA(w, name, sizeof(name)) == 0 ||
        lstrcmpiA(name, CONSOLE_CLASS) != 0 || !GetWindowRect(w, &r) ||
        !tiles_of(p, &r, &c0, &c1, &r0, &r1)) {
        return;
    }
    for (row = r0; row <= r1; row++) {
        for (c = c0; c <= c1; c++) {
            p->warm[row * p->cols + c] = WARM_TICKS;
        }
    }
}

static int sample_warm(POLL *p, HDC screen, unsigned int tick)
{
    int r, c, c0, c1 = 0, j, any = 0;
    int first = (int) (tick % (TILE / WARM_LINES));

    for (r = 0; r < p->rows; r++) {
        c0 = -1;
        for (c = 0; c < p->cols; c++) {
            if (p->warm[r * p->cols + c] != 0) {
                p->warm[r * p->cols + c]--;
                if (c0 < 0) {
                    c0 = c;
                }
                c1 = c;
            }
        }
        for (j = 0; c0 >= 0 && j < WARM_LINES; j++) {
            any |= sample_at(p, screen, first + j * (TILE / WARM_LINES), c0,
                             c1, r, r);
        }
    }
    return any;
}

static int take_hot(POLL *p)
{
    int i, any = 0;

    for (i = 0; i < p->cols * p->rows; i++) {
        if (p->hot[i] != 0) {
            p->hot[i]--;
            p->cand[i] = 1;
            any = 1;
        }
    }
    return any;
}

static void tick_check(POLL *p, HDC screen, unsigned int tick, int hinted)
{
    int i, any = 0, c0, c1, r0, r1;

    if (!hinted) {
        any = sample(p, screen, tick, 0, p->cols - 1, 0, p->rows - 1);
    } else {
        if (tick % SLOW_TICKS == 0) {
            any = sample(p, screen, tick / SLOW_TICKS, 0, p->cols - 1, 0,
                         p->rows - 1);
        }
        for (i = 0; i < p->nconsoles; i++) {
            if (tiles_of(p, &p->consoles[i], &c0, &c1, &r0, &r1)) {
                any |= sample(p, screen, tick, c0, c1, r0, r1);
            }
        }
    }
    if (hinted) {
        any |= sample_warm(p, screen, tick);
        any |= take_hot(p);
    }
    if (any) {
        check(p, screen, 1);
    }
}

static int mirror_due(void)
{
    HANDLE ev = mirror_event();

    if (mirror_noevent()) {
        return 1;
    }
    return ev != NULL && WaitForSingleObject(ev, 0) == WAIT_OBJECT_0;
}

static void mirror_shape(POLL *p)
{
    MR_PTROUT *s = &p->mr_shape;
    MRSHAPE_FORMAT f;
    CORE_POINTER cp;
    const char *why;

    if (mirror_swpointer()) {
        memset(&cp, 0, sizeof(cp));
        cp.cb = sizeof(cp);
        log_msg(LOG_DEBUG, "poll: mirror: GDI draws the pointer into the "
                "picture: the clients' pointer is hidden");
        cursor_mirror(&cp);
        return;
    }
    if (!mirror_pointer(s)) {
        if (!p->mr_noshape) {
            p->mr_noshape = 1;
            log_msg(LOG_INFO, "poll: mirror: the driver gives no pointer "
                    "shapes (an ntrdpsmr.dll from before 2026-11-04? "
                    "ntrdps -installmirror, then reboot); the hook's "
                    "shapes are used");
        }
        cursor_mirror(NULL);
        return;
    }
    f.depth = p->depth;
    f.red_mask = p->red_mask;
    f.green_mask = p->green_mask;
    f.blue_mask = p->blue_mask;
    f.palette = &p->palette[0][0];
    if (!mrshape_convert(s, &f, &cp, &why)) {
        log_msg(LOG_DEBUG, "poll: mirror: pointer shape %lu is not usable "
                "(%s): the hook's shape", (unsigned long) s->serial, why);
        cursor_mirror(NULL);
        return;
    }
    if (cp.width == 0) {
        log_msg(LOG_DEBUG, "poll: mirror: pointer shape %lu: hidden (the "
                "driver's last position %ld, %ld)", (unsigned long) s->serial,
                (long) s->posx, (long) s->posy);
    } else {
        log_msg(LOG_DEBUG, "poll: mirror: pointer shape %lu: %s, %ld x %ld%s, "
                "hot spot (%ld, %ld), color format %lu, xlate 0x%lx",
                (unsigned long) s->serial,
                (s->flags & MR_P_COLOR) ? "color" : "mono", (long) s->cx,
                (long) s->cy, (s->flags & MR_P_CUT) ? " (cut to 32 x 32)" : "",
                (long) s->hotx, (long) s->hoty, (unsigned long) s->format,
                (unsigned long) s->xlate);
    }
    cursor_mirror(&cp);
}

static void mark_tiles(POLL *p, const RECT *r)
{
    int c, c0, c1, row, r0, r1;

    if (tiles_of(p, r, &c0, &c1, &r0, &r1)) {
        for (row = r0; row <= r1; row++) {
            for (c = c0; c <= c1; c++) {
                p->cand[row * p->cols + c] = 1;
            }
        }
    }
}

static int get_pass(POLL *p, HDC screen, int *full)
{
    RECT r[MR_RECTS];
    unsigned long flags, ask = p->mr_ask;
    int n, i, was_full = *full;

    n = mirror_take(r, MR_RECTS, &flags);
    if (n < 0) {
        p->mirror = 0;
        cursor_mirror(NULL);
        *full = 1;
        return 1;
    }
    if (flags & MR_F_POINTER) {
        mirror_shape(p);
    }
    p->mr_passes++;
    p->mr_rects += (unsigned long) n;
    p->mr_ask |= flags & (MR_F_MODE | MR_F_PALETTE);
    if (n > 0) {
        RECT u = r[0];

        p->mr_draw_ms = GetTickCount();
        for (i = 1; i < n; i++) {
            UnionRect(&u, &u, &r[i]);
        }
        log_msg(LOG_DEBUG, "poll: mirror: %d rectangle%s in (%ld,%ld)-"
                "(%ld,%ld)%s", n, n == 1 ? "" : "s", (long) u.left,
                (long) u.top, (long) u.right, (long) u.bottom,
                (flags & MR_F_ALL) ? ", whole screen asked" : "");
    }
    if ((flags & MR_F_ALL) ||
        (n > 0 && (screen == NULL || p->lost || p->mem == NULL))) {
        *full = 1;
    }
    if (n > 0 && !*full) {
        for (i = 0; i < n; i++) {
            mark_tiles(p, &r[i]);
        }
        check(p, screen, 1);
    }
    return (*full && !was_full) || p->mr_ask != ask;
}

static int flush_fits(const POLL *p, unsigned long bpp)
{
    if (!p->flush_ok) {
        return 0;
    }
    if (p->depth == 32) {
        return bpp == 32;
    }
    return bpp == (unsigned long) p->cap_bpp;
}

static void flush_row(POLL *p, int y, int x, int n, const unsigned char *s,
                      unsigned long bpp)
{
    unsigned char *d = cap_row(p, y);
    long off, cb;
    int i;

    if (bpp == 32) {
        d += (long) x * 3;
        for (i = 0; i < n; i++, d += 3, s += 4) {
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        }
        return;
    }
    off = (long) x * (long) bpp / 8;
    cb = (long) n * (long) bpp / 8;
    if (cb > p->cap_stride - off) {
        cb = p->cap_stride - off;
    }
    memcpy(d + off, s, (size_t) cb);
}

static int flush_pixels(POLL *p, const MR_FLUSHOUT *f)
{
    const MR_OUT *h = &f->head;
    const MR_RECT *m;
    const unsigned char *s = mirror_flush_pixels(f);
    unsigned long i, rb;
    int y;
    RECT r;

    if (h->cx != p->w || h->cy != p->h || !flush_fits(p, h->bpp)) {
        return 0;
    }
    for (i = 0; i < h->nrects; i++) {
        m = &h->rect[i];
        rb = MR_ROW_BYTES(m->left, m->right, h->bpp);
        for (y = m->top; y < m->bottom; y++, s += rb) {
            flush_row(p, y, m->left, m->right - m->left, s, h->bpp);
        }
        r.left = m->left;
        r.top = m->top;
        r.right = m->right;
        r.bottom = m->bottom;
        mark_tiles(p, &r);
    }
    return 1;
}

static void text_glyphs(const MR_FLUSHOUT *f, unsigned long first,
                        unsigned long n)
{
    const MR_GLYPHPOS *pos = mirror_flush_places(f) + first;
    const unsigned char *area = mirror_flush_glyphs(f);
    const MR_GLYPH *mg;
    CORE_GLYPH *g;
    unsigned long i;

    for (i = 0; i < n; i++) {
        mg = (const MR_GLYPH *) (area + pos[i].glyph);
        g = &g_tglyph[i];
        g->ox = (int) mg->ox;
        g->oy = (int) mg->oy;
        g->cx = (int) mg->cx;
        g->cy = (int) mg->cy;
        memset(g->bits, 0, sizeof(g->bits));
        memcpy(g->bits, mg + 1, CORE_GLYPH_SIZE(g->cx, g->cy));
        g_tref[i].x = (int) pos[i].x;
    }
    g_tlogged = 0;
}

static int text_order(POLL *p, const MR_ORDER *m, unsigned long n,
                      unsigned long mask, unsigned char *cap_top,
                      unsigned int bpp)
{
    const CORE_GLYPH *g[CORE_TEXT_MAX];
    unsigned long i, k, part;
    unsigned char *keep = NULL;
    size_t row = (size_t) (m->right - m->left);
    long y;
    int id, stored = 1;
    CORE_ORDER o;

    if (!g_tlogged) {
        for (i = 0; i < n; i++) {
            id = core_glyph_put(&g_tglyph[i], &g_tref[i].serial);
            if (id < 0) {
                stored = 0;
                break;
            }
            g_tref[i].id = (unsigned int) id;
        }
        for (k = 0; stored && k * CORE_TEXT_MAX < n; k++) {
            part = n - k * CORE_TEXT_MAX;
            g_trun[k] = core_text_put(g_tref + k * CORE_TEXT_MAX,
                                      part > CORE_TEXT_MAX ?
                                      CORE_TEXT_MAX : (int) part);
        }
        g_tlogged = stored;
    }
    memset(&o, 0, sizeof(o));
    o.kind = CORE_ORDER_TEXT;
    o.rect.left = m->left;
    o.rect.top = m->top;
    o.rect.right = m->right;
    o.rect.bottom = m->bottom;
    o.op.left = m->oleft;
    o.op.top = m->otop;
    o.op.right = m->oright;
    o.op.bottom = m->obottom;
    o.y = (int) m->y;
    o.color = m->color & mask;
    o.bg = m->bg & mask;
    if (p->cap_bpp == 4 && !g_tlogged) {
        keep = (unsigned char *) malloc(row * (size_t) (m->bottom - m->top));
        if (keep == NULL) {
            return 0;
        }
        for (y = m->top; y < m->bottom; y++) {
            memcpy(keep + (size_t) (y - m->top) * row,
                   frame_row(p, (int) y) + m->left, row);
        }
    }
    for (k = 0; k * CORE_TEXT_MAX < n; k++) {
        part = n - k * CORE_TEXT_MAX;
        o.nglyph = part > CORE_TEXT_MAX ? CORE_TEXT_MAX : (int) part;
        for (i = 0; i < (unsigned long) o.nglyph; i++) {
            g[i] = &g_tglyph[k * CORE_TEXT_MAX + i];
        }
        if (k > 0) {
            o.op.left = o.op.right = 0;
            o.op.top = o.op.bottom = 0;
        }
        if (p->cap_bpp == 4) {
            draw_text(p->bits, p->stride, bpp, &o,
                      g_tref + k * CORE_TEXT_MAX, g);
            frame_to_cap4(p, &o.rect);
        } else {
            draw_text(cap_top, -p->cap_stride, bpp, &o,
                      g_tref + k * CORE_TEXT_MAX, g);
            if (g_tlogged) {
                draw_text(p->bits, p->stride, bpp, &o,
                          g_tref + k * CORE_TEXT_MAX, g);
            }
        }
        if (!g_tlogged) {
            continue;
        }
        o.run = g_trun[k];
        core_order(&o);
    }
    if (keep != NULL) {
        for (y = m->top; y < m->bottom; y++) {
            memcpy(frame_row(p, (int) y) + m->left,
                   keep + (size_t) (y - m->top) * row, row);
        }
        free(keep);
    }
    if (!g_tlogged) {
        mark_tiles(p, &o.rect);
    }
    return 1;
}

static void bitmap_take(const POLL *p, const MR_FLUSHOUT *f,
                        unsigned long off)
{
    const MR_BITMAP *mb = (const MR_BITMAP *) (mirror_flush_bitmaps(f) +
                                               off);
    const unsigned char *s = (const unsigned char *) (mb + 1);
    unsigned long rowb = MR_ROW_BYTES(0, mb->cx, f->head.bpp), x, y;
    unsigned char *d = g_bpix;

    g_bmp.cx = (int) mb->cx;
    g_bmp.cy = (int) mb->cy;
    if (f->head.bpp == 32) {
        for (y = 0; y < mb->cy; y++, s += rowb) {
            for (x = 0; x < mb->cx; x++, d += 3) {
                d[0] = s[4 * x];
                d[1] = s[4 * x + 1];
                d[2] = s[4 * x + 2];
            }
        }
        g_bmp.bits = g_bpix;
        g_bmp.stride = (long) mb->cx * 3;
    } else {
        g_bmp.bits = s;
        g_bmp.stride = (long) rowb;
    }
    g_bid = core_bitmap_put(g_bmp.cx, g_bmp.cy, (unsigned int) p->bytes * 8,
                            g_bmp.bits, g_bmp.stride, &g_bserial);
    g_boff = off;
    g_bvalid = 1;
}

static void bitmap_order(POLL *p, const MR_FLUSHOUT *f, const MR_ORDER *m,
                         unsigned char *cap_top, unsigned int bpp)
{
    CORE_ORDER o;

    if (!g_bvalid || m->first != g_boff) {
        bitmap_take(p, f, m->first);
    }
    memset(&o, 0, sizeof(o));
    o.kind = CORE_ORDER_BITMAP;
    o.rect.left = m->left;
    o.rect.top = m->top;
    o.rect.right = m->right;
    o.rect.bottom = m->bottom;
    o.src_x = (int) m->sx;
    o.src_y = (int) m->sy;
    o.rop = (unsigned int) m->rop;
    draw_bitmap(cap_top, -p->cap_stride, bpp, &o, &g_bmp);
    if (g_bid < 0) {
        p->bm_nomem++;
        mark_tiles(p, &o.rect);
        return;
    }
    draw_bitmap(p->bits, p->stride, bpp, &o, &g_bmp);
    o.bmp = (unsigned int) g_bid;
    o.bmp_serial = g_bserial;
    core_order(&o);
    p->bm_orders++;
}

static SAVE_COPY *save_find(POLL *p, unsigned long num)
{
    int i;

    for (i = 0; i < SAVE_COPIES; i++) {
        if (p->sv[i].num == num && num != 0) {
            return &p->sv[i];
        }
    }
    return NULL;
}

static void save_drop(POLL *p, SAVE_COPY *c, int tell)
{
    CORE_ORDER o;

    if (tell) {
        memset(&o, 0, sizeof(o));
        o.kind = CORE_ORDER_FREE;
        o.rect = c->r;
        o.save = c->num;
        core_order(&o);
        p->sv_freed++;
    }
    free(c->bits);
    c->bits = NULL;
    c->num = 0;
}

static void save_take(POLL *p, const MR_ORDER *m)
{
    SAVE_COPY *c = NULL, *old = NULL;
    size_t row = (size_t) (m->right - m->left) * (size_t) p->bytes;
    long y;
    int i;
    CORE_ORDER o;

    for (i = 0; i < SAVE_COPIES && c == NULL; i++) {
        if (p->sv[i].num == 0) {
            c = &p->sv[i];
        } else if (old == NULL || p->sv[i].age < old->age) {
            old = &p->sv[i];
        }
    }
    if (c == NULL) {
        c = old;
        save_drop(p, c, 1);
    }
    c->bits = (unsigned char *) malloc(row * (size_t) (m->bottom - m->top));
    if (c->bits == NULL) {
        p->sv_nomem++;
        return;
    }
    for (y = m->top; y < m->bottom; y++) {
        memcpy(c->bits + (size_t) (y - m->top) * row,
               frame_row(p, (int) y) + (size_t) m->left * p->bytes, row);
    }
    c->num = m->first;
    c->r.left = m->left;
    c->r.top = m->top;
    c->r.right = m->right;
    c->r.bottom = m->bottom;
    c->age = ++p->sv_age;
    memset(&o, 0, sizeof(o));
    o.kind = CORE_ORDER_SAVE;
    o.rect = c->r;
    o.save = c->num;
    core_order(&o);
    p->sv_made++;
}

static int save_put(POLL *p, const MR_ORDER *m)
{
    SAVE_COPY *c = save_find(p, m->first);
    size_t row = (size_t) (m->right - m->left) * (size_t) p->bytes;
    size_t at = (size_t) m->left * (size_t) p->bytes;
    long y;
    CORE_ORDER o;

    if (c == NULL || c->r.left != m->left || c->r.top != m->top ||
        c->r.right != m->right || c->r.bottom != m->bottom) {
        p->sv_missing++;
        return 0;
    }
    for (y = m->top; y < m->bottom; y++) {
        memcpy(frame_row(p, (int) y) + at,
               c->bits + (size_t) (y - m->top) * row, row);
        if (p->cap_bpp != 4) {
            memcpy(cap_row(p, (int) y) + at,
                   c->bits + (size_t) (y - m->top) * row, row);
        }
    }
    if (p->cap_bpp == 4) {
        frame_to_cap4(p, &c->r);
    }
    memset(&o, 0, sizeof(o));
    o.kind = CORE_ORDER_RESTORE;
    o.rect = c->r;
    o.save = c->num;
    core_order(&o);
    save_drop(p, c, 0);
    p->sv_restored++;
    return 1;
}

static void saves_keep(POLL *p, const MR_FLUSHOUT *f)
{
    int i, k, lock = 0;

    for (i = 0; i < SAVE_COPIES; i++) {
        if (p->sv[i].num == 0) {
            continue;
        }
        for (k = 0; f != NULL && k < MR_SAVES; k++) {
            if (f->savenum[k] == p->sv[i].num) {
                break;
            }
        }
        if (f != NULL && k < MR_SAVES) {
            continue;
        }
        if (!lock) {
            core_frame_enter();
            lock = 1;
        }
        save_drop(p, &p->sv[i], 1);
    }
    if (lock) {
        core_frame_leave();
    }
}

#define PAT_PARTS 16

static void pattern_order(POLL *p, const MR_ORDER *m, unsigned long mask,
                          unsigned char *cap_top, unsigned int bpp)
{
    CORE_ORDER o, part[PAT_PARTS];
    int n, i;

    memset(&o, 0, sizeof(o));
    o.kind = CORE_ORDER_PATTERN;
    o.rect.left = m->left;
    o.rect.top = m->top;
    o.rect.right = m->right;
    o.rect.bottom = m->bottom;
    o.color = m->color & mask;
    o.bg = m->bg & mask;
    o.rop = (unsigned int) m->rop;
    o.pat_x = (int) m->sx;
    o.pat_y = (int) m->sy;
    for (i = 0; i < 4; i++) {
        o.pat[i] = (unsigned char) (m->first >> (8 * i));
        o.pat[4 + i] = (unsigned char) (m->nglyph >> (8 * i));
    }
    p->pt_pieces++;
    n = draw_pat_split(p->bits, p->stride, bpp, &o, bpp == 8, part,
                       PAT_PARTS);
    if (n == 0) {
        part[0] = o;
        n = 1;
    }
    for (i = 0; i < n; i++) {
        if (!DRAW_USES_D(part[i].rop & 0xFF)) {
            if (DRAW_USES_D(o.rop & 0xFF)) {
                p->pt_copies++;
            } else {
                p->pt_plain++;
            }
        } else if (bpp != 8) {
            p->pt_plain++;
        } else if (draw_pat_rgb8(p->bits, p->stride, &part[i],
                                 &p->palette[0][0])) {
            part[i].flags |= CORE_OF_RGB8;
            p->pt_rgb8++;
        } else {
            p->pt_other++;
        }
        draw_order(p->bits, p->stride, bpp, &part[i]);
        if (p->cap_bpp == 4) {
            frame_to_cap4(p, &part[i].rect);
        } else {
            draw_order(cap_top, -p->cap_stride, bpp, &part[i]);
        }
        core_order(&part[i]);
    }
}

static unsigned long flush_orders(POLL *p, const MR_FLUSHOUT *f)
{
    const MR_ORDER *m = mirror_flush_orders(f);
    unsigned char *cap_top = cap_row(p, 0);
    unsigned int bpp = (unsigned int) p->bytes * 8;
    unsigned long i, mask, first = 0, n = 0;
    CORE_ORDER o;

    mask = bpp == 8 ? 0xFFUL : bpp == 16 ? 0xFFFFUL : 0xFFFFFFUL;
    memset(&o, 0, sizeof(o));
    g_bvalid = 0;
    core_frame_enter();
    for (i = 0; i < f->norders; i++, m++) {
        if (m->kind == MR_O_TEXT) {
            if (n == 0 || m->first != first || m->nglyph != n) {
                first = m->first;
                n = m->nglyph;
                text_glyphs(f, first, n);
            }
            if (!text_order(p, m, n, mask, cap_top, bpp)) {
                break;
            }
            continue;
        }
        if (m->kind == MR_O_SAVE) {
            save_take(p, m);
            continue;
        }
        if (m->kind == MR_O_RESTORE) {
            if (!save_put(p, m)) {
                break;
            }
            continue;
        }
        if (m->kind == MR_O_BITMAP) {
            bitmap_order(p, f, m, cap_top, bpp);
            continue;
        }
        if (m->kind == MR_O_PATTERN) {
            pattern_order(p, m, mask, cap_top, bpp);
            continue;
        }
        o.kind = m->kind == MR_O_FILL ? CORE_ORDER_FILL :
                 m->kind == MR_O_COPY ? CORE_ORDER_COPY : CORE_ORDER_LINE;
        o.rect.left = m->left;
        o.rect.top = m->top;
        o.rect.right = m->right;
        o.rect.bottom = m->bottom;
        o.src_x = (int) m->sx;
        o.src_y = (int) m->sy;
        o.color = m->color & mask;
        o.rop = (unsigned int) m->rop;
        draw_order(p->bits, p->stride, bpp, &o);
        if (p->cap_bpp == 4) {
            frame_to_cap4(p, &o.rect);
        } else {
            draw_order(cap_top, -p->cap_stride, bpp, &o);
        }
        core_order(&o);
    }
    core_frame_leave();
    return f->norders - i;
}

static int orders_wanted(const POLL *p)
{
    return !g_noorders && (p->depth >= 8 || p->depth == 4) &&
           !p->ord_pause;
}

static int flush_pass(POLL *p, HDC screen, int *full)
{
    MR_FLUSHOUT *f = (MR_FLUSHOUT *) p->mr_buf;
    unsigned long flags, ask = p->mr_ask, rects = 0, bytes = 0, i;
    unsigned long orders = 0, left;
    int k, rc, any = 0, misfit = 0, was_full = *full;
    int want = orders_wanted(p);
    RECT u, r;

    for (k = 0; k < FLUSH_PASSES; k++) {
        rc = mirror_flush(f, p->mr_more ? FLUSH_BIG : FLUSH_SMALL,
                          want ? MR_FL_ORDERS |
                                 (g_notext ? 0 : MR_FL_TEXT) |
                                 (g_nobitmaps ? 0 : MR_FL_BITMAPS) |
                                 (g_nopatterns ? 0 : MR_FL_PATTERNS) : 0);
        if (rc <= 0) {
            if (rc < 0) {
                p->mirror = 0;
                cursor_mirror(NULL);
            }
            p->mr_flush = 0;
            p->mr_more = 0;
            p->ord_asked = 0;
            saves_keep(p, NULL);
            *full = 1;
            return 1;
        }
        p->ord_asked = want;
        flags = f->head.flags;
        p->mr_more = (flags & MR_F_MORE) != 0;
        if (flags & MR_F_POINTER) {
            mirror_shape(p);
        }
        p->mr_ask |= flags & (MR_F_MODE | MR_F_PALETTE);
        p->mr_flushes++;
        bytes += f->datalen;
        if (f->norders > 0) {
            if (*full || screen == NULL || p->lost || p->mem == NULL ||
                (p->depth < 8 && p->depth != 4) ||
                f->head.cx != p->w || f->head.cy != p->h ||
                !flush_fits(p, f->head.bpp)) {
                p->mr_ordskip += f->norders;
                *full = 1;
            } else {
                if (any) {
                    check(p, screen, 0);
                    any = 0;
                }
                left = flush_orders(p, f);
                p->mr_orders += f->norders - left;
                orders += f->norders - left;
                if (left > 0) {
                    p->mr_ordskip += left;
                    *full = 1;
                }
            }
        }
        saves_keep(p, f);
        for (i = 0; i < f->head.nrects; i++) {
            r.left = f->head.rect[i].left;
            r.top = f->head.rect[i].top;
            r.right = f->head.rect[i].right;
            r.bottom = f->head.rect[i].bottom;
            if (rects++ == 0) {
                u = r;
            } else {
                UnionRect(&u, &u, &r);
            }
        }
        if (f->head.nrects > 0) {
            if (screen == NULL || p->lost || p->mem == NULL) {
                *full = 1;
            } else if (flush_pixels(p, f)) {
                any = 1;
            } else {
                misfit = 1;
                *full = 1;
            }
        }
        if (!p->mr_more) {
            break;
        }
    }
    p->mr_passes++;
    p->mr_rects += rects;
    p->mr_bytes += bytes;
    p->mr_last = rects;
    if (rects > 0 || orders > 0) {
        p->mr_draw_ms = GetTickCount();
    }
    if (rects > 0) {
        log_msg(LOG_DEBUG, "poll: mirror: %lu rectangle%s in (%ld,%ld)-"
                "(%ld,%ld), %d flush%s, %lu bytes of pixels, %lu order%s%s",
                rects, rects == 1 ? "" : "s", (long) u.left, (long) u.top,
                (long) u.right, (long) u.bottom, k + (k < FLUSH_PASSES),
                k == 0 ? "" : "es", bytes, orders, orders == 1 ? "" : "s",
                p->mr_more ? "; more waits" : "");
    } else if (orders > 0) {
        log_msg(LOG_DEBUG, "poll: mirror: %lu order%s, no rectangles",
                orders, orders == 1 ? "" : "s");
    }
    if (misfit && ++p->mr_misfit >= FLUSH_MISFIT) {
        log_msg(LOG_WARN, "poll: mirror: the driver's pixels (%ld x %ld, "
                "%lu bpp) do not fit the console (%d x %d, %d bpp); its "
                "rectangles are read with BitBlt from now on",
                (long) f->head.cx, (long) f->head.cy,
                (unsigned long) f->head.bpp, p->w, p->h, p->depth);
        p->mr_flush = 0;
        saves_keep(p, NULL);
    } else if (any && !misfit) {
        p->mr_misfit = 0;
    }
    if (any && !*full) {
        check(p, screen, 0);
    }
    return (*full && !was_full) || p->mr_ask != ask;
}

static int mirror_pass(POLL *p, HDC screen, int *full)
{
    p->mr_last = 0;
    if (p->mr_flush && p->flush_ok) {
        return flush_pass(p, screen, full);
    }
    p->mr_more = 0;
    return get_pass(p, screen, full);
}

static void orders_pause(POLL *p, HDC screen, int *full)
{
    if (p->mirror && p->ord_asked) {
        p->ord_pause = 1;
        mirror_pass(p, screen, full);
        p->ord_pause = 0;
        p->mr_pauses++;
    }
}

static void orders_resume(POLL *p, HDC screen, int *full)
{
    if (p->mirror && p->mr_flush && p->flush_ok && !p->ord_asked &&
        !*full && orders_wanted(p)) {
        mirror_pass(p, screen, full);
        p->mr_resumes++;
    }
}

#define SC_EXAMPLES 6
static unsigned long cap_frame_diff(POLL *p, RECT *box, char *ex)
{
    unsigned char tmp[TILE];
    const unsigned char *s, *d;
    unsigned long n = 0;
    int c, x, y, i, j, w, b = p->bytes;
    char *e = ex;

    box->left = p->w;
    box->top = p->h;
    box->right = 0;
    box->bottom = 0;
    for (y = 0; y < p->h; y++) {
        for (c = 0; c < p->cols; c++) {
            w = tile_width(p, c);
            s = cap_pixels(p, c * TILE, y, w, tmp);
            d = frame_row(p, y) + (long) c * TILE * b;
            if (memcmp(s, d, (size_t) w * b) == 0) {
                continue;
            }
            for (i = 0; i < w; i++) {
                if (memcmp(s + i * b, d + i * b, (size_t) b) != 0) {
                    x = c * TILE + i;
                    if (n < SC_EXAMPLES) {
                        e += sprintf(e, " %d,%d ", x, y);
                        for (j = b - 1; j >= 0; j--) {
                            e += sprintf(e, "%02x", s[i * b + j]);
                        }
                        *e++ = '/';
                        for (j = b - 1; j >= 0; j--) {
                            e += sprintf(e, "%02x", d[i * b + j]);
                        }
                        *e = '\0';
                    }
                    n++;
                    if (x < box->left) {
                        box->left = x;
                    }
                    if (x + 1 > box->right) {
                        box->right = x + 1;
                    }
                    if (y < box->top) {
                        box->top = y;
                    }
                    if (y + 1 > box->bottom) {
                        box->bottom = y + 1;
                    }
                }
            }
        }
    }
    return n;
}

static void shadow_check(POLL *p, HDC screen, int *full)
{
    unsigned long n;
    RECT box;
    char ex[SC_EXAMPLES * 32 + 8];

    p->ord_pause = 1;
    mirror_pass(p, screen, full);
    if (*full || !p->mirror || !p->mr_flush || p->mr_more ||
        !read_area(p, screen, 0, 0, p->w, p->h)) {
        p->ord_pause = 0;
        return;
    }
    mirror_pass(p, screen, full);
    p->ord_pause = 0;
    if (p->mr_last != 0 || *full || !p->mr_flush) {
        p->sc_busy++;
        log_msg(LOG_DEBUG, "poll: shadow check: GDI drew meanwhile; "
                "next time");
        return;
    }
    p->sc_rounds++;
    ex[0] = '\0';
    n = cap_frame_diff(p, &box, ex);
    if (n == 0) {
        log_msg(LOG_INFO, "poll: shadow check %lu: the screen equals the "
                "shadow (%lu of %lu rounds differed, %lu skipped)",
                p->sc_rounds, p->sc_bad, p->sc_rounds, p->sc_busy);
        return;
    }
    p->sc_bad++;
    log_msg(LOG_INFO, "poll: shadow check %lu: %lu pixels differ in "
            "(%ld,%ld)-(%ld,%ld) (%lu of %lu rounds differed, %lu skipped)",
            p->sc_rounds, n, (long) box.left, (long) box.top,
            (long) box.right, (long) box.bottom, p->sc_bad, p->sc_rounds,
            p->sc_busy);
    log_msg(LOG_DEBUG, "poll: shadow check %lu: first pixels (x,y "
            "screen/shadow):%s", p->sc_rounds, ex);
    memset(p->cand, 1, (size_t) p->cols * p->rows);
    check(p, screen, 0);
}

static int safety_sample(POLL *p, HDC screen)
{
    int line = sample_line(p->safety_n++), r, r0 = 0, y, any = 0;
    POINT pt;

    if (!mirror_swpointer() || !GetCursorPos(&pt)) {
        return sample_at(p, screen, line, 0, p->cols - 1, 0, p->rows - 1);
    }
    for (r = 0; r < p->rows; r++) {
        y = r * TILE + line % tile_height(p, r);
        if (y > pt.y - 32 && y < pt.y + 32) {
            if (r > r0) {
                any |= sample_at(p, screen, line, 0, p->cols - 1, r0,
                                 r - 1);
            }
            r0 = r + 1;
        }
    }
    if (r0 < p->rows) {
        any |= sample_at(p, screen, line, 0, p->cols - 1, r0, p->rows - 1);
    }
    return any;
}

static void safety_check(POLL *p, HDC screen, int *full)
{
    unsigned long now = GetTickCount();

    if (now - p->safety_ms < SAFETY_MS ||
        (now - p->mr_draw_ms < SAFETY_QUIET &&
         now - p->safety_ms < SAFETY_MAX_MS)) {
        return;
    }
    p->safety_ms = now;
    orders_pause(p, screen, full);
    if (*full) {
        return;
    }
    if (safety_sample(p, screen)) {
        p->mr_safety++;
        log_msg(LOG_DEBUG, "poll: the safety sample found changes");
        check(p, screen, 1);
    }
}

static int wait_tick(POLL *p, HDC screen, int *full)
{
    DWORD start = GetTickCount(), used, rc;
    HANDLE h[2];
    double t;
    int now;

    for (;;) {
        used = GetTickCount() - start;
        if (used >= TICK_MS) {
            return WaitForSingleObject(p->stop, 0) == WAIT_TIMEOUT;
        }
        h[0] = p->stop;
        h[1] = p->mirror ? mirror_event() : NULL;
        if (h[1] == NULL) {
            return WaitForSingleObject(p->stop, TICK_MS - used) ==
                   WAIT_TIMEOUT;
        }
        rc = p->mr_more ? WAIT_OBJECT_0 + 1
                        : WaitForMultipleObjects(2, h, FALSE, TICK_MS - used);
        if (rc == WAIT_TIMEOUT) {
            return 1;
        }
        if (rc == WAIT_FAILED) {
            log_msg(LOG_WARN, "poll: waiting on the mirror's event failed, "
                    "error %lu; polling", (unsigned long) GetLastError());
            mirror_close();
            p->mirror = 0;
            cursor_mirror(NULL);
            saves_keep(p, NULL);
            *full = 1;
            continue;
        }
        if (rc != WAIT_OBJECT_0 + 1) {
            return 0;
        }
        if (!p->mr_more && p->settle_ms > 0 &&
            WaitForSingleObject(p->stop, p->settle_ms) != WAIT_TIMEOUT) {
            return 0;
        }
        t = log_ms();
        now = mirror_pass(p, screen, full);
        p->work_ms += log_ms() - t;
        if (now) {
            return 1;
        }
        if (WaitForSingleObject(p->stop, MR_GAP_MS) != WAIT_TIMEOUT) {
            return 0;
        }
    }
}

static double cpu_seconds(void)
{
    FILETIME created, ended, kernel, user;

    if (!GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel,
                         &user)) {
        return -1.0;
    }
    return ((double) kernel.dwHighDateTime + (double) user.dwHighDateTime) *
           429.4967296 + ((double) kernel.dwLowDateTime +
           (double) user.dwLowDateTime) / 1e7;
}

static void rect_line(void)
{
    static const char *const names[MR_RW_N] = {
        "bitmap copies", "bitmap ROPs", "big bitmaps", "bitmap and brush",
        "pattern brushes", "masks and other copies", "shapes", "restores",
        "text", "full", "orders off", "other"
    };
    MIRROR_STATS mst;
    unsigned long all = 0;
    char line[700];
    int i, n;

    mirror_stats(&mst);
    if (mst.flushes == 0) {
        return;
    }
    for (i = 0; i < MR_RW_N; i++) {
        all += mst.rectwhy[i];
    }
    n = sprintf(line, "since the channel opened %lu drawing calls became "
                "rectangles (calls / thousand pixels):", all);
    for (i = 0; i < MR_RW_N; i++) {
        n += sprintf(line + n, "%s %s %lu / %lu", i == 0 ? "" : ",",
                     names[i], mst.rectwhy[i],
                     (mst.rectpix[i] + 500UL) / 1000UL);
    }
    log_msg(LOG_INFO, "poll: mirror: %s", line);
}

static void cpu_line(POLL *p, int hinted)
{
    double now = cpu_seconds();
    DWORD ms = GetTickCount() - p->cpu_ms;
    unsigned long used, part, work, wpart;
    MIRROR_STATS mst;

    if (p->mirror) {
        mirror_stats(&mst);
        log_msg(LOG_INFO, "poll: mirror: %lu passes, %lu rectangles, %lu "
                "safety samples found changes; the driver since its start: "
                "%lu drawing calls, %lu screen reads, %lu pointer shapes, "
                "%lu pointer moves", p->mr_passes, p->mr_rects, p->mr_safety,
                mst.calls, mst.reads, mst.ptrshape, mst.ptrmove);
        if (p->mr_flush) {
            log_msg(LOG_INFO, "poll: mirror: %lu flushes, %lu KB of pixels; "
                    "the driver's shadow since its start: %lu calls not "
                    "drawn, %lu menu saves, %lu failed", p->mr_flushes,
                    (p->mr_bytes + 1023) / 1024, mst.skipped, mst.saves,
                    mst.savefail);
            log_msg(LOG_INFO, "poll: mirror: %lu drawing orders, %lu left "
                    "out (a full read was due), %lu pauses before screen "
                    "reads and %lu FLUSHes after them%s; the driver since "
                    "its start: %lu calls became orders, %lu of their kind "
                    "became rectangles",
                    p->mr_orders, p->mr_ordskip, p->mr_pauses,
                    p->mr_resumes, g_noorders ? " (-noorders: off)" : "",
                    mst.ordcalls, mst.ordmiss);
            log_msg(LOG_INFO, "poll: mirror: text%s: the driver since its "
                    "start: %lu calls became orders (%lu glyph positions, "
                    "%lu KB of glyphs since the channel opened), %lu became "
                    "rectangles (kind %lu, batches %lu, glyph %lu, "
                    "placement %lu, opaque %lu, full %lu, pieces %lu, "
                    "other %lu)", g_notext ? " (-notext: off)" : "",
                    mst.textcalls, mst.places, (mst.glyphbytes + 1023) / 1024,
                    mst.textmiss, mst.textwhy[MR_TM_KIND],
                    mst.textwhy[MR_TM_BATCH], mst.textwhy[MR_TM_GLYPH],
                    mst.textwhy[MR_TM_PLACE], mst.textwhy[MR_TM_OPAQUE],
                    mst.textwhy[MR_TM_FULL], mst.textwhy[MR_TM_PIECES],
                    mst.textwhy[MR_TM_OTHER]);
            rect_line();
            log_msg(LOG_INFO, "poll: mirror: menu saves since the capture "
                    "started: %lu copies made, %lu put back, %lu dropped, "
                    "%lu restores without a copy, %lu saves without memory; "
                    "the driver since its start: %lu saves and %lu "
                    "restores became orders", p->sv_made, p->sv_restored,
                    p->sv_freed, p->sv_missing, p->sv_nomem, mst.saveords,
                    mst.restords);
            log_msg(LOG_INFO, "poll: mirror: bitmaps%s since the capture "
                    "started: %lu orders, %lu pieces without a place in the "
                    "store (went as bitmaps), %lu KB of bitmaps since the "
                    "channel opened; the driver since its start: %lu calls "
                    "became orders", g_nobitmaps ? " (-nobitmaps: off)" : "",
                    p->bm_orders, p->bm_nomem,
                    (mst.bitmapbytes + 1023) / 1024, mst.bmpcalls);
            log_msg(LOG_INFO, "poll: mirror: pattern brushes%s since the "
                    "capture started: %lu pieces became %lu orders that "
                    "read no pixels as they came, %lu that read none after "
                    "the pixels under them were looked at, %lu exact at 8 "
                    "bpp on every client, %lu not (8 bpp sessions send "
                    "those as bitmaps); the driver since its start: %lu "
                    "calls became orders", g_nopatterns ?
                    " (-nopatterns: off)" : "", p->pt_pieces, p->pt_plain,
                    p->pt_copies, p->pt_rgb8, p->pt_other, mst.patcalls);
        }
        if (mst.skipped != p->mr_skipped) {
            log_msg(LOG_WARN, "poll: mirror: the shadow did not draw %lu "
                    "more calls (%lu in all): their areas may be wrong "
                    "until they are drawn again (the safety samples fix "
                    "them)", mst.skipped - p->mr_skipped, mst.skipped);
            p->mr_skipped = mst.skipped;
        }
    }
    p->mr_passes = 0;
    p->mr_rects = 0;
    p->mr_safety = 0;
    p->mr_flushes = 0;
    p->mr_bytes = 0;
    p->mr_orders = 0;
    p->mr_ordskip = 0;
    p->mr_pauses = 0;
    p->mr_resumes = 0;
    if (now >= 0.0 && p->cpu_s >= 0.0 && ms > 0) {
        used = (unsigned long) ((now - p->cpu_s) * 1000.0 + 0.5);
        part = (unsigned long) (used * 1000.0 / ms + 0.5);
        work = (unsigned long) (p->work_ms + 0.5);
        wpart = (unsigned long) (p->work_ms * 1000.0 / ms + 0.5);
        log_msg(LOG_INFO, "poll: CPU %lu.%03lu s in %lu s (%lu.%lu%%), "
                "ticks' work %lu ms (%lu.%lu%%); hints %s: %lu rectangles, "
                "lost in %lu ticks", used / 1000, used % 1000,
                (unsigned long) (ms + 500) / 1000, part / 10, part % 10,
                work, wpart / 10, wpart % 10, hinted ? "on" : "off",
                p->hint_rects, p->hint_lost);
    }
    p->cpu_s = now;
    p->cpu_ms += ms;
    p->work_ms = 0.0;
    p->hint_rects = 0;
    p->hint_lost = 0;
}

static int check_palette(POLL *p, HDC screen)
{
    PALETTEENTRY pe[256];
    unsigned char now[256][3];
    int i;

    if (!p->palette_device ||
        GetSystemPaletteEntries(screen, 0, 256, pe) != 256) {
        return 0;
    }
    for (i = 0; i < 256; i++) {
        now[i][0] = pe[i].peRed;
        now[i][1] = pe[i].peGreen;
        now[i][2] = pe[i].peBlue;
    }
    if (memcmp(now, p->palette, sizeof(now)) == 0) {
        return 0;
    }
    core_frame_enter();
    memcpy(p->palette, now, sizeof(now));
    core_frame_leave();
    dib_palette(p);
    log_msg(LOG_DEBUG, "poll: the system palette changed");
    core_frame_changed();
    return 1;
}

static int make_tables(POLL *p)
{
    p->cols = (p->w + TILE - 1) / TILE;
    p->rows = (p->h + TILE - 1) / TILE;
    p->bits = (unsigned char *) calloc((size_t) p->stride * p->h, 1);
    p->cand = (unsigned char *) calloc((size_t) p->cols * p->rows, 1);
    p->dirty = (RECT *) calloc((size_t) p->cols * p->rows, sizeof(RECT));
    p->lines = (int *) calloc((size_t) p->rows, sizeof(int));
    p->warm = (unsigned char *) calloc((size_t) p->cols * p->rows, 1);
    p->hot = (unsigned char *) calloc((size_t) p->cols * p->rows, 1);
    return p->bits != NULL && p->cand != NULL && p->dirty != NULL &&
           p->lines != NULL && p->warm != NULL && p->hot != NULL;
}

static void free_tables(POLL *p)
{
    free(p->bits);
    free(p->cand);
    free(p->dirty);
    free(p->lines);
    free(p->warm);
    free(p->hot);
    p->bits = NULL;
    p->cand = NULL;
    p->dirty = NULL;
    p->lines = NULL;
    p->warm = NULL;
    p->hot = NULL;
}

static void frame_of(POLL *p, CORE_FRAME *f)
{
    char text[16 * 7 + 1];
    int i;

    f->width = (unsigned int) p->w;
    f->height = (unsigned int) p->h;
    f->palette = f->bpp == 8 ? &p->palette[0][0] : NULL;
    f->bits = p->bits;
    f->stride = p->stride;
    if (f->bpp == 16) {
        log_msg(LOG_INFO, "poll: frame 16 bpp, masks 0x%04lx 0x%04lx "
                "0x%04lx (%s)", f->red_mask, f->green_mask, f->blue_mask,
                f->green_mask == 0x03E0 ? "15 bit color" : "16 bit color");
    } else if (p->depth == 4) {
        for (i = 0; i < 16; i++) {
            sprintf(text + i * 7, "%02x%02x%02x ", p->palette[i][0],
                    p->palette[i][1], p->palette[i][2]);
        }
        text[16 * 7 - 1] = '\0';
        log_msg(LOG_INFO, "poll: frame 8 bpp; the 16 console colors: %s",
                text);
    } else {
        log_msg(LOG_INFO, "poll: frame %u bpp%s", f->bpp,
                p->depth == 32 ? " (32 bpp read as 24)" : "");
    }
}

static int mode_changed(const POLL *p, HDC screen)
{
    CORE_FRAME f;
    int depth = GetDeviceCaps(screen, BITSPIXEL) *
                GetDeviceCaps(screen, PLANES);

    if (GetDeviceCaps(screen, HORZRES) != p->w ||
        GetDeviceCaps(screen, VERTRES) != p->h || depth != p->depth) {
        return 1;
    }
    if (depth != 15 && depth != 16) {
        return 0;
    }
    memset(&f, 0, sizeof(f));
    return probe_masks(screen, &f) && f.green_mask != p->green_mask;
}

static int reformat(POLL *p)
{
    CORE_FRAME f;
    int ok;

    teardown(p);
    memset(&f, 0, sizeof(f));
    core_frame_enter();
    free_tables(p);
    p->palette_device = 0;
    p->reads = 0;
    p->broken = 0;
    p->read_error = 0;
    ok = read_format(p, &f);
    if (ok && !make_tables(p)) {
        log_msg(LOG_ERROR, "poll: out of memory for a %d x %d frame", p->w,
                p->h);
        free_tables(p);
        ok = 0;
    }
    if (ok) {
        dib_format(p, &f);
        frame_of(p, &f);
    }
    core_frame_format(ok ? &f : NULL);
    core_frame_leave();
    return ok;
}

static DWORD WINAPI capture_thread(LPVOID arg)
{
    POLL *p = (POLL *) arg;
    DESKTOP_THREAD d;
    HDC screen = NULL;
    unsigned int tick;
    int full = 1;
    int changed;
    int hinted;
    double t0;

    memset(&d, 0, sizeof(d));
    d.who = "capture";
    p->cpu_s = cpu_seconds();
    p->cpu_ms = GetTickCount();
    if (g_nohints) {
        log_msg(LOG_INFO, "poll: -nohints: hints are not used; the whole "
                "screen is sampled every tick (as in task M2.4)");
    }
    if (g_nomirror) {
        log_msg(LOG_INFO, "poll: -nomirror: the mirror driver is not used");
    } else if (mirror_open()) {
        p->mirror = 1;
        p->settle_ms = mirror_layered() ? 0 : MR_SETTLE_MS;
        if (mirror_noevent()) {
            log_msg(LOG_INFO, "poll: changes come from the mirror driver, "
                    "asked every tick (%d ms: it has no event before NT "
                    "4.0 SP3); the whole screen is sampled every %d ms "
                    "as a safety net", TICK_MS, SAFETY_MS);
        } else {
            log_msg(LOG_INFO, "poll: changes come from the mirror driver, "
                    "read %lu ms after its signal; the whole screen is "
                    "sampled every %d ms as a safety net",
                    (unsigned long) p->settle_ms, SAFETY_MS);
        }
        mirror_shape(p);
        p->mr_buf = (unsigned char *) malloc(FLUSH_BIG);
        p->mr_flush = p->mr_buf != NULL;
        if (!p->flush_ok) {
            log_msg(LOG_INFO, "poll: mirror: at %d bpp with these color "
                    "masks the driver's pixels do not fit; its rectangles "
                    "are read with BitBlt", p->depth);
        }
        if (g_shadowcheck) {
            log_msg(LOG_INFO, "poll: -shadowcheck: every %d s the whole "
                    "screen is compared with the driver's shadow",
                    SHADOW_TICKS * TICK_MS / 1000);
        }
    }
    for (tick = 0; ; tick++) {
        t0 = log_ms();
        p->t_calls = 0;
        p->t_rows = 0;
        if (p->mirror && (p->mr_more || mirror_due())) {
            mirror_pass(p, screen, &full);
        }
        if (tick % FOLLOW_TICKS == 0 || (p->mr_ask & MR_F_MODE) != 0 ||
            p->follow_now) {
            p->follow_now = 0;
            if (screen != NULL) {
                ReleaseDC(NULL, screen);
                screen = NULL;
            }
            if (desktop_follow(&d)) {
                full = 1;
            }
        }
        if (screen == NULL) {
            screen = GetDC(NULL);
        }
        changed = 0;
        if ((tick % MODE_TICKS == 0 || (p->mr_ask & MR_F_MODE) != 0) &&
            screen != NULL && !p->lost && mode_changed(p, screen)) {
            p->lost = !reformat(p);
            full = 1;
            changed = 1;
#ifdef NTRDPS_TLS
            if (p->mirror) {
                mirror_fit();
            }
#endif
        }
        p->mr_ask &= ~(unsigned long) MR_F_MODE;
        cursor_check(screen, d.name);
        hinted = !p->lost && !p->mirror && hints_on(d.name);
        if (hinted) {
            take_hints(p);
            if (tick % CONSOLE_TICKS == 0) {
                find_consoles(p);
            }
            if (InterlockedExchange((LONG *) &g_input_hint, 0) != 0) {
                warm_console_focus(p);
            }
        }
        if ((tick % PALETTE_TICKS == 0 || (p->mr_ask & MR_F_PALETTE) != 0) &&
            screen != NULL && !p->lost && check_palette(p, screen)) {
            teardown(p);
            full = 1;
        }
        p->mr_ask &= ~(unsigned long) MR_F_PALETTE;
        if (screen != NULL && !p->lost &&
            (p->mem != NULL || setup(p, screen))) {
            p->rd = p->shared && reader_select(d.name);
            if (full) {
                orders_pause(p, screen, &full);
                full = !full_read(p, screen);
            } else if (p->mirror) {
                safety_check(p, screen, &full);
                if (g_shadowcheck && p->mirror && p->mr_flush &&
                    tick % SHADOW_TICKS == SHADOW_TICKS / 2) {
                    shadow_check(p, screen, &full);
                }
            } else {
                tick_check(p, screen, tick, hinted);
            }
            orders_resume(p, screen, &full);
        }
        if (changed) {
            core_frame_changed();
        }
        if (log_ms() - t0 > 1000.0) {
            log_msg(LOG_DEBUG, "poll: tick %lu took %lu ms (full %d, "
                    "desktop \"%s\"): %lu reads, %lu rows",
                    (unsigned long) tick, (unsigned long) (log_ms() - t0),
                    full, d.name, p->t_calls, p->t_rows);
        }
        p->work_ms += log_ms() - t0;
        if (GetTickCount() - p->cpu_ms >= CPU_MS) {
            cpu_line(p, hinted);
        }
        if (tick == 0) {
            SetEvent(p->ready);
        }
        if (!wait_tick(p, screen, &full)) {
            break;
        }
    }
    if (p->mirror) {
        rect_line();
    }
    saves_keep(p, NULL);
    mirror_close();
    free(p->mr_buf);
    p->mr_buf = NULL;
    if (screen != NULL) {
        ReleaseDC(NULL, screen);
    }
    teardown(p);
    reader_free();
    desktop_leave(&d);
    return 0;
}

static void free_poll(POLL *p)
{
    if (p->stop != NULL) {
        CloseHandle(p->stop);
    }
    if (p->ready != NULL) {
        CloseHandle(p->ready);
    }
    free_tables(p);
    free(p);
}

static int start(CORE_FRAME *f)
{
    POLL *p;
    DWORD tid;

    p = (POLL *) calloc(1, sizeof(POLL));
    if (p == NULL) {
        log_msg(LOG_ERROR, "poll: out of memory");
        return 0;
    }
    if (!read_format(p, f)) {
        free(p);
        return 0;
    }
    dib_format(p, f);
    p->getdibits = g_getdibits;
    p->stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    p->ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!make_tables(p) || p->stop == NULL || p->ready == NULL) {
        log_msg(LOG_ERROR, "poll: out of memory for a %d x %d frame", p->w,
                p->h);
        free_poll(p);
        return 0;
    }
    frame_of(p, f);

    hookdll_start();
    cursor_reset();
    p->thread = CreateThread(NULL, 0, capture_thread, p, 0, &tid);
    if (p->thread == NULL) {
        log_msg(LOG_ERROR, "poll: CreateThread failed, error %lu",
                (unsigned long) GetLastError());
        hookdll_stop();
        free_poll(p);
        return 0;
    }
    if (WaitForSingleObject(p->ready, READY_WAIT_MS) != WAIT_OBJECT_0) {
        log_msg(LOG_WARN, "poll: no first screen read within %d s; the "
                "screen comes later", READY_WAIT_MS / 1000);
    }
    g_poll = p;
    return 1;
}

static void stop(void)
{
    POLL *p = g_poll;

    if (p == NULL) {
        return;
    }
    g_poll = NULL;
    SetEvent(p->stop);
    if (WaitForSingleObject(p->thread, STOP_WAIT_MS) != WAIT_OBJECT_0) {
        log_msg(LOG_ERROR, "poll: the capture thread did not end within "
                "%d s; its memory is not freed", STOP_WAIT_MS / 1000);
        return;
    }
    CloseHandle(p->thread);
    hookdll_stop();
    free_poll(p);
}

const CORE_BACKEND poll_backend = { "poll", start, stop };
