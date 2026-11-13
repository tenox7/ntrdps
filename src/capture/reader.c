/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "desktop.h"
#include "reader.h"
#include "palette.h"

#define RD_MAGIC     0x31524452UL
#define RD_HEAD      8192UL
#define RD_LINES     256
#define RD_SLOTS     3
#define RD_NAME      32
#define RD_START_MS  10000
#define RD_WAIT_MS   5000
#define RD_QUIT_MS   2000
#define RD_RETRY_MS  30000
#define RD_NO_CODE   0xFFFFFFFFUL

enum { RD_HELLO = 1, RD_AREA, RD_LINE, RD_QUIT };

typedef struct {
    BITMAPINFOHEADER h;
    RGBQUAD          c[256];
} RD_DIB;

typedef struct {
    unsigned long magic;
    unsigned long format;
    int           w, h;
    long          stride;
    int           pal_dc;
    RD_DIB        dib;
    int           op;
    int           x0, y0, x1, y1;
    int           n;
    int           line[RD_LINES];
    int           ok;
    unsigned long error;
} RD_SHARED;

typedef char rd_head_fits[sizeof(RD_SHARED) <= RD_HEAD ? 1 : -1];

typedef struct {
    char          desktop[RD_NAME];
    HANDLE        process;
    HANDLE        ask, done;
    DWORD         pid;
    int           down;
    DWORD         down_ms;
    unsigned long error;
} RD_SLOT;

static HANDLE        g_map;
static RD_SHARED    *g_sh;
static unsigned long g_bytes;
static unsigned long g_serial;
static RD_SLOT       g_slot[RD_SLOTS];
static RD_SLOT      *g_cur;

static void map_name(char *out, unsigned long pid, unsigned long serial)
{
    sprintf(out, "ntrdps-rd-%lu-%lu", pid, serial);
}

static void event_name(char *out, unsigned long pid, unsigned long serial,
                       const char *desktop, const char *what)
{
    sprintf(out, "ntrdps-rd-%lu-%lu-%s-%s", pid, serial, desktop, what);
}

static unsigned long error_now(void)
{
    DWORD err = GetLastError();

    return err != 0 ? (unsigned long) err : RD_NO_CODE;
}

int reader_wanted(void)
{
    return desktop_nt31();
}

unsigned char *reader_buffer(unsigned long bytes)
{
    char name[64];

    if (g_sh != NULL && bytes == g_bytes) {
        return (unsigned char *) g_sh + RD_HEAD;
    }
    reader_free();
    g_serial++;
    map_name(name, (unsigned long) GetCurrentProcessId(), g_serial);
#ifdef _WIN64
    g_map = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
#else
    g_map = CreateFileMappingA((HANDLE) 0xFFFFFFFFUL, NULL, PAGE_READWRITE,
#endif
                               0, RD_HEAD + bytes, name);
    if (g_map != NULL) {
        g_sh = (RD_SHARED *) MapViewOfFile(g_map, FILE_MAP_WRITE, 0, 0, 0);
    }
    if (g_sh == NULL) {
        log_msg(LOG_WARN, "reader: no shared capture buffer (%lu bytes), "
                "error %lu: the Winlogon and Screen-saver desktops stay "
                "black (NT 3.1)", bytes, (unsigned long) GetLastError());
        if (g_map != NULL) {
            CloseHandle(g_map);
            g_map = NULL;
        }
        return NULL;
    }
    memset(g_sh, 0, sizeof(*g_sh));
    g_sh->magic = RD_MAGIC;
    g_bytes = bytes;
    return (unsigned char *) g_sh + RD_HEAD;
}

void reader_format(int w, int h, long stride, const void *dib,
                   unsigned int dib_len, int pal_dc)
{
    if (g_sh == NULL) {
        return;
    }
    g_sh->w = w;
    g_sh->h = h;
    g_sh->stride = stride;
    memset(&g_sh->dib, 0, sizeof(g_sh->dib));
    memcpy(&g_sh->dib, dib,
           dib_len < sizeof(g_sh->dib) ? dib_len : sizeof(g_sh->dib));
    g_sh->pal_dc = pal_dc;
    g_sh->format++;
}

static void slot_down(RD_SLOT *s, const char *what, unsigned long err)
{
    if (err == 0) {
        err = RD_NO_CODE;
    }
    if (!s->down || err != s->error) {
        log_msg(LOG_WARN, "reader: desktop \"%s\": %s (error %lu); its "
                "screen is not read; the next try comes in %d s",
                s->desktop, what, err, RD_RETRY_MS / 1000);
    }
    s->down = 1;
    s->down_ms = GetTickCount();
    s->error = err;
    SetLastError((DWORD) err);
}

static void slot_stop(RD_SLOT *s, int quit)
{
    if (s->process != NULL) {
        if (quit && g_sh != NULL &&
            WaitForSingleObject(s->process, 0) == WAIT_TIMEOUT) {
            g_sh->op = RD_QUIT;
            SetEvent(s->ask);
            WaitForSingleObject(s->process, RD_QUIT_MS);
        }
        if (WaitForSingleObject(s->process, 0) == WAIT_TIMEOUT) {
            TerminateProcess(s->process, 1);
            WaitForSingleObject(s->process, RD_QUIT_MS);
        }
        CloseHandle(s->process);
        s->process = NULL;
    }
    if (s->ask != NULL) {
        CloseHandle(s->ask);
        s->ask = NULL;
    }
    if (s->done != NULL) {
        CloseHandle(s->done);
        s->done = NULL;
    }
}

static int slot_start(RD_SLOT *s)
{
    static char exe[MAX_PATH], cmd[96];
    char name[128], desk[RD_NAME];
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    HANDLE wait[2];
    DWORD rc, code;
    unsigned long me = (unsigned long) GetCurrentProcessId();

    event_name(name, me, g_serial, s->desktop, "ask");
    s->ask = CreateEventA(NULL, FALSE, FALSE, name);
    event_name(name, me, g_serial, s->desktop, "done");
    s->done = CreateEventA(NULL, FALSE, FALSE, name);
    if (s->ask == NULL || s->done == NULL) {
        code = GetLastError();
        slot_stop(s, 0);
        slot_down(s, "no event for the helper", code);
        return 0;
    }
    ResetEvent(s->ask);
    ResetEvent(s->done);
    if (GetModuleFileNameA(NULL, exe, sizeof(exe)) == 0) {
        code = GetLastError();
        slot_stop(s, 0);
        slot_down(s, "GetModuleFileName failed", code);
        return 0;
    }
    sprintf(cmd, "ntrdps -reader %s %lu %lu", s->desktop, me, g_serial);
    strcpy(desk, s->desktop);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.lpDesktop = desk;
    g_sh->op = RD_HELLO;
    g_sh->ok = 0;
    g_sh->error = 0;
    if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, DETACHED_PROCESS,
                        NULL, NULL, &si, &pi)) {
        code = GetLastError();
        slot_stop(s, 0);
        slot_down(s, "CreateProcess of the helper failed", code);
        log_msg(LOG_DEBUG, "reader: it was: %s, \"%s\"", exe, cmd);
        return 0;
    }
    CloseHandle(pi.hThread);
    s->process = pi.hProcess;
    s->pid = pi.dwProcessId;
    wait[0] = s->done;
    wait[1] = s->process;
    rc = WaitForMultipleObjects(2, wait, FALSE, RD_START_MS);
    if (rc == WAIT_OBJECT_0 && g_sh->ok) {
        s->down = 0;
        s->error = 0;
        log_msg(LOG_INFO, "reader: process %lu reads desktop \"%s\" for "
                "the capture (NT 3.1 lets only a process that started on "
                "a desktop read its screen)", (unsigned long) s->pid,
                s->desktop);
        return 1;
    }
    if (rc == WAIT_OBJECT_0) {
        code = (DWORD) g_sh->error;
    } else if (rc == WAIT_TIMEOUT) {
        code = WAIT_TIMEOUT;
    } else {
        code = 0;
        GetExitCodeProcess(s->process, &code);
    }
    slot_stop(s, 0);
    slot_down(s, rc == WAIT_OBJECT_0
              ? "the helper got no screen DC there (no access?)"
              : rc == WAIT_TIMEOUT ? "the helper did not answer"
              : "the helper ended at its start (exit code)", code);
    return 0;
}

static int slot_ready(RD_SLOT *s)
{
    DWORD code;

    if (s->process != NULL) {
        if (WaitForSingleObject(s->process, 0) == WAIT_TIMEOUT) {
            return 1;
        }
        code = 0;
        GetExitCodeProcess(s->process, &code);
        slot_stop(s, 0);
        slot_down(s, "the helper process ended (exit code)", code);
        return 0;
    }
    if (s->down && GetTickCount() - s->down_ms < RD_RETRY_MS) {
        SetLastError((DWORD) s->error);
        return 0;
    }
    return slot_start(s);
}

static int request(int op)
{
    RD_SLOT *s = g_cur;
    HANDLE wait[2];
    DWORD rc, code;

    if (s == NULL || g_sh == NULL) {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!slot_ready(s)) {
        return 0;
    }
    g_sh->op = op;
    g_sh->ok = 0;
    g_sh->error = 0;
    SetEvent(s->ask);
    wait[0] = s->done;
    wait[1] = s->process;
    rc = WaitForMultipleObjects(2, wait, FALSE, RD_WAIT_MS);
    if (rc == WAIT_OBJECT_0) {
        if (g_sh->ok) {
            return 1;
        }
        SetLastError((DWORD) g_sh->error);
        return 0;
    }
    if (rc == WAIT_TIMEOUT) {
        slot_stop(s, 0);
        slot_down(s, "the helper did not answer a read", WAIT_TIMEOUT);
        return 0;
    }
    code = 0;
    GetExitCodeProcess(s->process, &code);
    slot_stop(s, 0);
    slot_down(s, "the helper process ended (exit code)", code);
    return 0;
}

int reader_select(const char *desktop)
{
    int i, free_slot = -1;

    g_cur = NULL;
    if (g_sh == NULL || desktop == NULL || !desktop_foreign(desktop) ||
        strlen(desktop) >= RD_NAME) {
        return 0;
    }
    for (i = 0; i < RD_SLOTS; i++) {
        if (g_slot[i].desktop[0] == '\0') {
            if (free_slot < 0) {
                free_slot = i;
            }
        } else if (lstrcmpiA(g_slot[i].desktop, desktop) == 0) {
            g_cur = &g_slot[i];
            return 1;
        }
    }
    if (free_slot < 0) {
        return 0;
    }
    strcpy(g_slot[free_slot].desktop, desktop);
    g_cur = &g_slot[free_slot];
    return 1;
}

int reader_area(int x0, int y0, int x1, int y1)
{
    if (g_sh == NULL) {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    g_sh->x0 = x0;
    g_sh->y0 = y0;
    g_sh->x1 = x1;
    g_sh->y1 = y1;
    return request(RD_AREA);
}

int reader_lines(int x0, int x1, const int *line, int n)
{
    int k, i;

    if (g_sh == NULL) {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    for (k = 0; k < n; k += RD_LINES) {
        g_sh->x0 = x0;
        g_sh->x1 = x1;
        g_sh->n = n - k < RD_LINES ? n - k : RD_LINES;
        for (i = 0; i < g_sh->n; i++) {
            g_sh->line[i] = line[k + i];
        }
        if (!request(RD_LINE)) {
            return 0;
        }
    }
    return 1;
}

void reader_free(void)
{
    int i;

    for (i = 0; i < RD_SLOTS; i++) {
        slot_stop(&g_slot[i], 1);
        memset(&g_slot[i], 0, sizeof(g_slot[i]));
    }
    g_cur = NULL;
    if (g_sh != NULL) {
        UnmapViewOfFile(g_sh);
        g_sh = NULL;
    }
    if (g_map != NULL) {
        CloseHandle(g_map);
        g_map = NULL;
    }
    g_bytes = 0;
}

typedef struct {
    RD_SHARED     *sh;
    unsigned char *rows;
    unsigned long  format;
    HDC            mem, paldc;
    HBITMAP        bm, old_bm;
    HPALETTE       pal, old_pal;
} RD_HELPER;

static void helper_drop(RD_HELPER *x)
{
    if (x->paldc != NULL) {
        SelectPalette(x->paldc, x->old_pal, FALSE);
        DeleteObject(x->pal);
        DeleteDC(x->paldc);
        x->paldc = NULL;
        x->pal = NULL;
    }
    if (x->mem != NULL) {
        SelectObject(x->mem, x->old_bm);
        DeleteObject(x->bm);
        DeleteDC(x->mem);
        x->mem = NULL;
        x->bm = NULL;
    }
}

static int helper_setup(RD_HELPER *x, HDC screen)
{
    static struct {
        WORD         ver, n;
        PALETTEENTRY e[256];
    } lp;
    static unsigned char rgb[256][3];
    RD_SHARED *sh = x->sh;
    int i;

    if (x->mem != NULL && x->format == sh->format) {
        return 1;
    }
    helper_drop(x);
    x->mem = CreateCompatibleDC(screen);
    x->bm = x->mem != NULL ? CreateCompatibleBitmap(screen, sh->w, sh->h)
                           : NULL;
    if (x->bm == NULL) {
        if (x->mem != NULL) {
            DeleteDC(x->mem);
            x->mem = NULL;
        }
        return 0;
    }
    x->old_bm = (HBITMAP) SelectObject(x->mem, x->bm);
    if (sh->pal_dc) {
        for (i = 0; i < 256; i++) {
            rgb[i][0] = sh->dib.c[i].rgbRed;
            rgb[i][1] = sh->dib.c[i].rgbGreen;
            rgb[i][2] = sh->dib.c[i].rgbBlue;
        }
        palette_capture(rgb);
        lp.ver = 0x300;
        lp.n = 256;
        for (i = 0; i < 256; i++) {
            lp.e[i].peRed = rgb[i][0];
            lp.e[i].peGreen = rgb[i][1];
            lp.e[i].peBlue = rgb[i][2];
            lp.e[i].peFlags = 0;
        }
        x->paldc = CreateCompatibleDC(screen);
        x->pal = x->paldc != NULL ? CreatePalette((LOGPALETTE *) &lp)
                                  : NULL;
        if (x->pal != NULL) {
            x->old_pal = SelectPalette(x->paldc, x->pal, FALSE);
        } else if (x->paldc != NULL) {
            DeleteDC(x->paldc);
            x->paldc = NULL;
        }
    }
    x->format = sh->format;
    return 1;
}

static int helper_rows(RD_HELPER *x, HDC screen, int y0, int y1)
{
    RD_SHARED *sh = x->sh;
    RD_DIB d = sh->dib;
    int got;

    SelectObject(x->mem, x->old_bm);
    got = GetDIBits(x->paldc != NULL ? x->paldc : screen, x->bm,
                    (UINT) (sh->h - y1), (UINT) (y1 - y0),
                    x->rows + (long) (sh->h - y1) * sh->stride,
                    (BITMAPINFO *) &d, DIB_RGB_COLORS);
    SelectObject(x->mem, x->bm);
    return got == y1 - y0;
}

static int helper_serve(RD_HELPER *x)
{
    RD_SHARED *sh = x->sh;
    HDC screen;
    DWORD err;
    int ok, i;

    if (sh->x0 < 0 || sh->x1 > sh->w || sh->x0 >= sh->x1 ||
        (sh->op == RD_AREA &&
         (sh->y0 < 0 || sh->y1 > sh->h || sh->y0 >= sh->y1)) ||
        (sh->op == RD_LINE && (sh->n < 0 || sh->n > RD_LINES)) ||
        (sh->op != RD_AREA && sh->op != RD_LINE)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    for (i = 0; sh->op == RD_LINE && i < sh->n; i++) {
        if (sh->line[i] < 0 || sh->line[i] >= sh->h) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return 0;
        }
    }
    screen = GetDC(NULL);
    if (screen == NULL) {
        return 0;
    }
    ok = helper_setup(x, screen);
    if (ok && sh->op == RD_AREA) {
        ok = BitBlt(x->mem, sh->x0, sh->y0, sh->x1 - sh->x0,
                    sh->y1 - sh->y0, screen, sh->x0, sh->y0, SRCCOPY) &&
             helper_rows(x, screen, sh->y0, sh->y1);
    } else if (ok) {
        for (i = 0; ok && i < sh->n; i++) {
            ok = BitBlt(x->mem, sh->x0, sh->line[i], sh->x1 - sh->x0, 1,
                        screen, sh->x0, sh->line[i], SRCCOPY);
        }
        for (i = 0; ok && i < sh->n; i++) {
            ok = helper_rows(x, screen, sh->line[i], sh->line[i] + 1);
        }
    }
    err = GetLastError();
    ReleaseDC(NULL, screen);
    SetLastError(err);
    return ok;
}

int reader_main(const char *desktop, const char *pid_text,
                const char *serial_text)
{
    char name[128];
    unsigned long pid = strtoul(pid_text, NULL, 10);
    unsigned long serial = strtoul(serial_text, NULL, 10);
    HANDLE map, ask, done, parent, wait[2];
    RD_HELPER x;
    HDC screen;
    DWORD rc;

    if (strlen(desktop) >= RD_NAME) {
        return 1;
    }
    memset(&x, 0, sizeof(x));
    map_name(name, pid, serial);
    map = OpenFileMappingA(FILE_MAP_WRITE, FALSE, name);
    if (map != NULL) {
        x.sh = (RD_SHARED *) MapViewOfFile(map, FILE_MAP_WRITE, 0, 0, 0);
    }
    if (x.sh == NULL || x.sh->magic != RD_MAGIC) {
        return 2;
    }
    x.rows = (unsigned char *) x.sh + RD_HEAD;
    event_name(name, pid, serial, desktop, "ask");
    ask = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, name);
    event_name(name, pid, serial, desktop, "done");
    done = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, name);
    parent = OpenProcess(SYNCHRONIZE, FALSE, (DWORD) pid);
    if (ask == NULL || done == NULL || parent == NULL) {
        return 3;
    }
    screen = GetDC(NULL);
    x.sh->error = screen != NULL ? 0 : error_now();
    x.sh->ok = screen != NULL;
    if (screen != NULL) {
        ReleaseDC(NULL, screen);
    }
    rc = (DWORD) x.sh->ok;
    SetEvent(done);
    if (rc == 0) {
        return 4;
    }
    wait[0] = ask;
    wait[1] = parent;
    for (;;) {
        rc = WaitForMultipleObjects(2, wait, FALSE, INFINITE);
        if (rc != WAIT_OBJECT_0) {
            break;
        }
        if (x.sh->op == RD_QUIT) {
            x.sh->ok = 1;
            SetEvent(done);
            break;
        }
        x.sh->ok = helper_serve(&x);
        x.sh->error = x.sh->ok ? 0 : error_now();
        SetEvent(done);
    }
    helper_drop(&x);
    return 0;
}
