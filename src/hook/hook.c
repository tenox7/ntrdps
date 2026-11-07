/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>

#define HINT_MAX    64
#define LOCK_TRIES  64

#define WM_SYSTIMER     0x0118
#define IDSYS_CARET     0xFFFF

#pragma data_seg(".shared")
static HHOOK            g_hook = NULL;
static volatile HCURSOR g_cursor = NULL;
static volatile LONG    g_serial = 0;
static volatile DWORD   g_thread = 0;
static HHOOK            g_hook_sent = NULL;
static volatile LONG    g_hint_lock = 0;
static volatile LONG    g_hint_n = 0;
static volatile LONG    g_hint_all = 0;
static RECT             g_hint[HINT_MAX] = { { 0, 0, 0, 0 } };
#pragma data_seg()

static HINSTANCE g_instance;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = instance;
    }
    return TRUE;
}

static int hint_lock(void)
{
    int i;

    for (i = 0; InterlockedExchange((LONG *) &g_hint_lock, 1) != 0; i++) {
        if (i == LOCK_TRIES) {
            return 0;
        }
        Sleep(i < LOCK_TRIES / 2 ? 0 : 1);
    }
    return 1;
}

static void hint_unlock(void)
{
    InterlockedExchange((LONG *) &g_hint_lock, 0);
}

static void hint(const RECT *r)
{
    LONG n;
    RECT *last;

    if (r->left >= r->right || r->top >= r->bottom) {
        return;
    }
    if (!hint_lock()) {
        g_hint_all = 1;
        return;
    }
    n = g_hint_n;
    last = &g_hint[n > 0 ? n - 1 : 0];
    if (n > 0 && last->left == r->left && last->top == r->top &&
        last->right == r->right && last->bottom == r->bottom) {
    } else if (n < HINT_MAX) {
        g_hint[n].left = r->left;
        g_hint[n].top = r->top;
        g_hint[n].right = r->right;
        g_hint[n].bottom = r->bottom;
        g_hint_n = n + 1;
    } else {
        g_hint_all = 1;
    }
    hint_unlock();
}

static void hint_window(HWND w)
{
    RECT r;

    if (w != NULL && GetWindowRect(w, &r)) {
        hint(&r);
    }
}

static void hint_client(HWND w, const RECT *r)
{
    POINT a, b;
    RECT s;

    a.x = r->left;
    a.y = r->top;
    b.x = r->right;
    b.y = r->bottom;
    if (ClientToScreen(w, &a) && ClientToScreen(w, &b)) {
        s.left = a.x;
        s.top = a.y;
        s.right = b.x;
        s.bottom = b.y;
        hint(&s);
    }
}

static void hint_update(HWND w)
{
    RECT r;

    if (w != NULL && GetUpdateRect(w, &r, FALSE)) {
        hint_client(w, &r);
    }
}

static void hint_caret(HWND w, int left)
{
    POINT p;
    RECT r;

    if (w != NULL && GetCaretPos(&p)) {
        r.left = p.x - left;
        r.top = p.y;
        r.right = p.x + 20;
        r.bottom = p.y + 24;
        hint_client(w, &r);
    }
}

static void hint_taken(const MSG *m)
{
    switch (m->message) {
    case WM_PAINT:
        hint_update(m->hwnd);
        break;
    case WM_KEYDOWN:
    case WM_CHAR:
    case WM_SYSKEYDOWN:
    case WM_SYSCHAR:
        hint_caret(m->hwnd, 20);
        hint_window(m->hwnd);
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP:
    case WM_NCLBUTTONDBLCLK:
    case WM_NCRBUTTONDOWN:
    case WM_NCRBUTTONUP:
        hint_window(m->hwnd);
        break;
    case WM_MOUSEMOVE:
        if (m->wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) {
            hint_window(m->hwnd);
        }
        break;
    case WM_SYSTIMER:
        if ((m->wParam & 0xFFFF) == IDSYS_CARET) {
            hint_caret(m->hwnd, 4);
        }
        break;
    }
}

static int is_mouse(UINT msg)
{
    return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
           (msg >= WM_NCMOUSEMOVE && msg <= WM_NCMBUTTONDBLCLK);
}

static LRESULT CALLBACK get_message(int code, WPARAM wp, LPARAM lp)
{
    const MSG *m = (const MSG *) lp;
    DWORD me = GetCurrentThreadId();
    HCURSOR h;

    if (code == HC_ACTION && wp == PM_REMOVE) {
        if (is_mouse(m->message)) {
            g_thread = me;
        }
        if (g_thread == me) {
            h = GetCursor();
            if (h != g_cursor || g_serial == 0) {
                g_cursor = h;
                InterlockedIncrement((LONG *) &g_serial);
            }
        }
        hint_taken(m);
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

static LRESULT CALLBACK sent_message(int code, WPARAM wp, LPARAM lp)
{
    const CWPSTRUCT *m = (const CWPSTRUCT *) lp;

    if (code == HC_ACTION) {
        switch (m->message) {
        case WM_PAINT:
            hint_update(m->hwnd);
            break;
        case WM_NCPAINT:
        case WM_NCACTIVATE:
        case WM_WINDOWPOSCHANGING:
        case WM_WINDOWPOSCHANGED:
        case WM_SHOWWINDOW:
        case WM_SETTEXT:
        case WM_ENABLE:
        case WM_HSCROLL:
        case WM_VSCROLL:
        case EM_SETSEL:
        case BM_SETCHECK:
        case BM_SETSTATE:
            hint_window(m->hwnd);
            break;
        }
    }
    return CallNextHookEx(g_hook_sent, code, wp, lp);
}

BOOL WINAPI HookStart(void)
{
    g_cursor = NULL;
    g_thread = 0;
    g_serial = 0;
    g_hint_n = 0;
    g_hint_all = 0;
    g_hint_lock = 0;
    g_hook = SetWindowsHookEx(WH_GETMESSAGE, get_message, g_instance, 0);
    if (g_hook == NULL) {
        return FALSE;
    }
    g_hook_sent = SetWindowsHookEx(WH_CALLWNDPROC, sent_message, g_instance,
                                   0);
    if (g_hook_sent == NULL) {
        UnhookWindowsHookEx(g_hook);
        g_hook = NULL;
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI HookStop(void)
{
    BOOL ok = TRUE;

    if (g_hook_sent != NULL) {
        ok = UnhookWindowsHookEx(g_hook_sent);
        g_hook_sent = NULL;
    }
    if (g_hook != NULL) {
        ok = UnhookWindowsHookEx(g_hook) && ok;
        g_hook = NULL;
    }
    return ok;
}

HCURSOR WINAPI HookCursor(LONG *serial, DWORD *thread)
{
    HCURSOR h;
    LONG n;

    do {
        n = g_serial;
        h = g_cursor;
    } while (n != g_serial);
    *serial = n;
    *thread = g_thread;
    return h;
}

int WINAPI HookHints(RECT *out, int max, int *all)
{
    int i, n;

    if (!hint_lock()) {
        hint_unlock();
        g_hint_all = 0;
        *all = 1;
        return 0;
    }
    *all = g_hint_all;
    g_hint_all = 0;
    n = g_hint_n < max ? (int) g_hint_n : max;
    for (i = 0; i < n; i++) {
        out[i].left = g_hint[i].left;
        out[i].top = g_hint[i].top;
        out[i].right = g_hint[i].right;
        out[i].bottom = g_hint[i].bottom;
    }
    if (g_hint_n > n) {
        *all = 1;
    }
    g_hint_n = 0;
    hint_unlock();
    return n;
}
