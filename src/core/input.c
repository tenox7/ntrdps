/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "core.h"
#include "desktop.h"
#include "keymap.h"
#include "sas.h"
#include "input.h"

#define INPUT_POLL_MS 250
#define STOP_WAIT_MS  5000
#define QUEUE_MAX     256
#define PUT_WAIT_MS   2000
#define PUT_SLEEP_MS  10
#define US_ENGLISH    0x0409

#ifndef MOUSEEVENTF_WHEEL
#define MOUSEEVENTF_WHEEL 0x0800
#endif

typedef char lock_bits_match[KM_LOCK_NUM == CORE_LOCK_NUM &&
                             KM_LOCK_CAPS == CORE_LOCK_CAPS &&
                             KM_LOCK_SCROLL == CORE_LOCK_SCROLL ? 1 : -1];

#define IQ_KEY     1
#define IQ_LOCKS   2
#define IQ_RELEASE 3
#define IQ_MOUSE   4

typedef struct {
    unsigned char kind;
    unsigned char code;
    unsigned char prefix;
    unsigned char down;
    unsigned int  bits;
    int           x, y;
    int           wheel;
} ITEM;

typedef HANDLE (WINAPI *GET_KEYBOARD_LAYOUT)(DWORD thread);
typedef UINT   (WINAPI *MAP_VIRTUAL_KEY_EX)(UINT code, UINT type, HANDLE hkl);

static HANDLE           g_thread;
static HANDLE           g_stop;
static HANDLE           g_wake;
static CRITICAL_SECTION g_lock;
static ITEM             g_queue[QUEUE_MAX];
static int              g_count;
static int              g_lost;
static int              g_ready;

static KEYMAP              g_km;
static GET_KEYBOARD_LAYOUT p_get_layout;
static MAP_VIRTUAL_KEY_EX  p_map_ex;
static HANDLE              g_hkl;
static DWORD               g_layout;
static unsigned int        g_buttons;
static unsigned int        g_ignore;
static int                 g_wheel;
static int                 g_wheel_logged;
static int                 g_altgr;

static const struct {
    unsigned int bit;
    DWORD        down, up;
    const char  *name;
} g_button[3] = {
    { CORE_BUTTON_LEFT, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, "left" },
    { CORE_BUTTON_RIGHT, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP,
      "right" },
    { CORE_BUTTON_MIDDLE, MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP,
      "middle" }
};

static void emit(void *ctx, unsigned int vk, unsigned int scan,
                 unsigned int flags)
{
    keybd_event((BYTE) vk, (BYTE) scan,
                ((flags & KM_EXTENDED) ? KEYEVENTF_EXTENDEDKEY : 0) |
                ((flags & KM_UP) ? KEYEVENTF_KEYUP : 0), 0);
    log_msg(LOG_DEBUG, "input: keybd_event VK 0x%02X, scan 0x%02X%s%s", vk,
            scan, (flags & KM_EXTENDED) ? ", extended" : "",
            (flags & KM_UP) ? ", up" : "");
}

static unsigned int map_vk(void *ctx, unsigned int code)
{
    if (g_hkl != NULL) {
        return p_map_ex(code, 1, g_hkl);
    }
    return MapVirtualKeyA(code, 1);
}

static void sas(void *ctx)
{
    log_msg(LOG_DEBUG, "input: Ctrl+Alt+Del from the client: to the SAS "
            "thread");
    sas_send();
}

static void begin_batch(void)
{
    HWND fg = GetForegroundWindow();
    DWORD tid = fg != NULL ? GetWindowThreadProcessId(fg, NULL) : 0;
    DWORD layout = 0;
    char name[16];

    g_hkl = NULL;
    if (p_get_layout != NULL && tid != 0) {
        g_hkl = p_get_layout(tid);
#ifdef _WIN64
        layout = (DWORD) (ULONG_PTR) g_hkl;
#else
        layout = (DWORD) g_hkl;
#endif
    }
    if (g_hkl == NULL && GetKeyboardLayoutNameA(name)) {
        layout = strtoul(name, NULL, 16);
    }
    g_km.altgr_fake_ctrl = g_altgr == 1 ||
        (g_altgr == 2 && layout != 0 && (layout & 0xFFFF) != US_ENGLISH);
    if (layout != g_layout) {
        g_layout = layout;
        log_msg(LOG_INFO, "input: the console keyboard layout is %08lX%s",
                (unsigned long) layout, g_km.altgr_fake_ctrl
                ? ": Right Alt also brings Left Ctrl (AltGr)" : "");
    }
}

static int console_locks(unsigned int *locks)
{
    HWND fg = GetForegroundWindow();
    DWORD me = GetCurrentThreadId();
    DWORD tid = fg != NULL ? GetWindowThreadProcessId(fg, NULL) : 0;

    if (tid == 0 || tid == me || !AttachThreadInput(me, tid, TRUE)) {
        return 0;
    }
    *locks = ((GetKeyState(VK_SCROLL) & 1) ? CORE_LOCK_SCROLL : 0) |
             ((GetKeyState(VK_NUMLOCK) & 1) ? CORE_LOCK_NUM : 0) |
             ((GetKeyState(VK_CAPITAL) & 1) ? CORE_LOCK_CAPS : 0);
    AttachThreadInput(me, tid, FALSE);
    return 1;
}

static DWORD absolute(int v, int size)
{
    if (v >= size) {
        v = size - 1;
    }
    if (v < 0) {
        v = 0;
    }
    return (((DWORD) v << 16) + (DWORD) size - 1) / (DWORD) size;
}

static void mouse_item(const ITEM *it)
{
    DWORD ax, ay, flags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    unsigned int want, i;
    int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);

    if (it->wheel != 0) {
        if (g_wheel) {
            mouse_event(MOUSEEVENTF_WHEEL, 0, 0, (DWORD) it->wheel, 0);
            log_msg(LOG_DEBUG, "input: mouse_event wheel %+d", it->wheel);
        } else if (!g_wheel_logged) {
            g_wheel_logged = 1;
            log_msg(LOG_INFO, "input: mouse wheel dropped: NT before 4.0 "
                    "has no wheel (rdp-2 4.7.3). Logged once per session.");
        }
        return;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    ax = absolute(it->x, w);
    ay = absolute(it->y, h);
    g_ignore &= it->bits;
    want = it->bits & ~g_ignore;
    if (want == g_buttons) {
        mouse_event(flags, ax, ay, 0, 0);
        return;
    }
    for (i = 0; i < 3; i++) {
        if ((want ^ g_buttons) & g_button[i].bit) {
            mouse_event(flags | ((want & g_button[i].bit) ? g_button[i].down
                                 : g_button[i].up), ax, ay, 0, 0);
            log_msg(LOG_DEBUG, "input: mouse_event %s %s at (%d, %d): "
                    "absolute (%lu, %lu)", g_button[i].name,
                    (want & g_button[i].bit) ? "down" : "up", it->x, it->y,
                    (unsigned long) ax, (unsigned long) ay);
        }
    }
    g_buttons = want;
}

static void release_buttons(void)
{
    unsigned int i;

    for (i = 0; i < 3; i++) {
        if (g_buttons & g_button[i].bit) {
            mouse_event(g_button[i].up, 0, 0, 0, 0);
            log_msg(LOG_INFO, "input: the %s mouse button released",
                    g_button[i].name);
        }
    }
    g_ignore |= g_buttons;
    g_buttons = 0;
}

static void run_queue(void)
{
    ITEM q[QUEUE_MAX];
    unsigned int console = 0;
    int n, i, lost, known, keys = 0;

    EnterCriticalSection(&g_lock);
    n = g_count;
    memcpy(q, g_queue, (size_t) n * sizeof(q[0]));
    g_count = 0;
    lost = g_lost;
    g_lost = 0;
    LeaveCriticalSection(&g_lock);

    begin_batch();
    for (i = 0; i < n; i++) {
        switch (q[i].kind) {
        case IQ_KEY:
            keymap_key(&g_km, q[i].code, q[i].prefix, q[i].down);
            keys++;
            break;
        case IQ_LOCKS:
            known = console_locks(&console);
            if (known) {
                log_msg(LOG_DEBUG, "input: SYNC: lock keys 0x%X, the "
                        "console has 0x%X", q[i].bits, console);
            } else {
                log_msg(LOG_DEBUG, "input: SYNC: lock keys 0x%X; the "
                        "console's are not known", q[i].bits);
            }
            release_buttons();
            keymap_sync(&g_km, q[i].bits, known, console);
            break;
        case IQ_RELEASE:
            release_buttons();
            g_ignore = 0;
            keymap_release_all(&g_km);
            g_wheel_logged = 0;
            break;
        case IQ_MOUSE:
            mouse_item(&q[i]);
            break;
        }
    }
    if (lost) {
        log_msg(LOG_WARN, "input: the queue stayed full for %d s: "
                "events were dropped, so all keys and buttons are released",
                PUT_WAIT_MS / 1000);
        release_buttons();
        keymap_release_all(&g_km);
    }
    if (keys > 0) {
        mouse_event(MOUSEEVENTF_MOVE, 0, 0, 0, 0);
        poll_input_hint();
    }
}

static DWORD WINAPI input_thread(LPVOID arg)
{
    DESKTOP_THREAD d;
    HANDLE h[2];
    DWORD r;

    memset(&d, 0, sizeof(d));
    d.who = "input";
    h[0] = g_stop;
    h[1] = g_wake;
    desktop_follow(&d);
    for (;;) {
        r = WaitForMultipleObjects(2, h, FALSE, INPUT_POLL_MS);
        if (r != WAIT_OBJECT_0 + 1 && r != WAIT_TIMEOUT) {
            if (r != WAIT_OBJECT_0) {
                log_msg(LOG_ERROR, "input: WaitForMultipleObjects "
                        "failed, error %lu: no more input",
                        (unsigned long) GetLastError());
            }
            break;
        }
        desktop_follow(&d);
        if (r == WAIT_OBJECT_0 + 1) {
            run_queue();
        }
    }
    release_buttons();
    keymap_release_all(&g_km);
    desktop_leave(&d);
    return 0;
}

int input_start(int altgr_fake_ctrl)
{
    DWORD v = GetVersion(), tid;
    HMODULE user32 = GetModuleHandleA("USER32.DLL");

    g_altgr = altgr_fake_ctrl;
    memset(&g_km, 0, sizeof(g_km));
    g_km.emit = emit;
    g_km.map_vk = map_vk;
    g_km.sas = sas;
    g_km.win_keys = (v & 0xFF) * 100 + ((v >> 8) & 0xFF) >= 350;
    keymap_init(&g_km);
    g_buttons = 0;
    g_ignore = 0;
    g_wheel = (v & 0xFF) >= 4;
    g_wheel_logged = 0;
    if ((v & 0xFF) >= 4 && user32 != NULL) {
        p_get_layout = (GET_KEYBOARD_LAYOUT)
            GetProcAddress(user32, "GetKeyboardLayout");
        p_map_ex = (MAP_VIRTUAL_KEY_EX)
            GetProcAddress(user32, "MapVirtualKeyExA");
        if (p_get_layout == NULL || p_map_ex == NULL) {
            p_get_layout = NULL;
            log_msg(LOG_WARN, "input: no GetKeyboardLayout or "
                    "MapVirtualKeyExA: keys use the layout of ntrdps");
        }
    }
    g_count = 0;
    g_lost = 0;
    InitializeCriticalSection(&g_lock);
    g_ready = 1;
    g_stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    g_wake = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (g_stop == NULL || g_wake == NULL) {
        log_msg(LOG_ERROR, "input: CreateEvent failed, error %lu: no "
                "input", (unsigned long) GetLastError());
        input_stop();
        return 0;
    }
    g_thread = CreateThread(NULL, 0, input_thread, NULL, 0, &tid);
    if (g_thread == NULL) {
        log_msg(LOG_ERROR, "input: CreateThread failed, error %lu: no "
                "input", (unsigned long) GetLastError());
        input_stop();
        return 0;
    }
    return 1;
}

void input_stop(void)
{
    if (!g_ready) {
        return;
    }
    if (g_thread != NULL) {
        SetEvent(g_stop);
        if (WaitForSingleObject(g_thread, STOP_WAIT_MS) != WAIT_OBJECT_0) {
            log_msg(LOG_WARN, "input: the thread did not end within %d s",
                    STOP_WAIT_MS / 1000);
        }
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    if (g_stop != NULL) {
        CloseHandle(g_stop);
        g_stop = NULL;
    }
    if (g_wake != NULL) {
        CloseHandle(g_wake);
        g_wake = NULL;
    }
    DeleteCriticalSection(&g_lock);
    g_ready = 0;
}

static void put(const ITEM *it)
{
    DWORD t0 = GetTickCount();

    if (g_thread == NULL) {
        return;
    }
    EnterCriticalSection(&g_lock);
    while (g_count >= QUEUE_MAX && !g_lost &&
           GetTickCount() - t0 < PUT_WAIT_MS) {
        LeaveCriticalSection(&g_lock);
        SetEvent(g_wake);
        Sleep(PUT_SLEEP_MS);
        EnterCriticalSection(&g_lock);
    }
    if (g_count < QUEUE_MAX) {
        g_queue[g_count++] = *it;
    } else {
        g_lost = 1;
    }
    LeaveCriticalSection(&g_lock);
    SetEvent(g_wake);
}

void input_key(unsigned int code, unsigned int prefix, int down)
{
    ITEM it;

    memset(&it, 0, sizeof(it));
    it.kind = IQ_KEY;
    it.code = (unsigned char) (code & 0x7F);
    it.prefix = (unsigned char) prefix;
    it.down = (unsigned char) (down != 0);
    put(&it);
}

void input_locks(unsigned int locks)
{
    ITEM it;

    memset(&it, 0, sizeof(it));
    it.kind = IQ_LOCKS;
    it.bits = locks;
    put(&it);
}

void input_mouse(int x, int y, unsigned int buttons, int wheel)
{
    ITEM it;

    memset(&it, 0, sizeof(it));
    it.kind = IQ_MOUSE;
    it.x = x;
    it.y = y;
    it.bits = buttons;
    it.wheel = wheel;
    put(&it);
}

void input_release(void)
{
    ITEM it;

    memset(&it, 0, sizeof(it));
    it.kind = IQ_RELEASE;
    put(&it);
}
