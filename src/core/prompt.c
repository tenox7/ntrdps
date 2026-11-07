/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "desktop.h"
#include "prompt.h"

#define CLASS_NAME   "ntrdps prompt"
#define TIMER_ID     1
#define END_WAIT_MS  5000
#define WM_PROMPT_END (WM_USER + 100)

#ifndef WS_EX_TOPMOST
#define WS_EX_TOPMOST 0x00000008L
#endif
#ifndef DC_HASDEFID
#define DC_HASDEFID   0x534B
#endif

typedef struct {
    HANDLE           thread;
    CRITICAL_SECTION lock;
    HWND             wnd;
    int              ended;
    char             question[64];
    unsigned int     left;
    int              answer;
    int              margin, line;
    PROMPT_DONE      done;
    void            *ctx;
} PROMPT;

static void paint(HWND w, PROMPT *p)
{
    PAINTSTRUCT ps;
    char text[64];
    HDC dc = BeginPaint(w, &ps);

    sprintf(text, "No answer in %u s means No.", p->left);
    SetBkMode(dc, TRANSPARENT);
    TextOutA(dc, p->margin, p->margin, p->question,
             (int) strlen(p->question));
    TextOutA(dc, p->margin, p->margin + p->line * 3 / 2, text,
             (int) strlen(text));
    EndPaint(w, &ps);
}

static void answer(HWND w, PROMPT *p, int a)
{
    p->answer = a;
    DestroyWindow(w);
}

static LRESULT CALLBACK box_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    PROMPT *p = (PROMPT *) GetWindowLongA(w, 0);

    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongA(w, 0, (LONG) ((CREATESTRUCT *) lp)->lpCreateParams);
        break;
    case WM_PAINT:
        paint(w, p);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDYES:
            answer(w, p, PROMPT_YES);
            return 0;
        case IDNO:
        case IDCANCEL:
        case IDOK:
            answer(w, p, PROMPT_NO);
            return 0;
        }
        break;
    case DM_GETDEFID:
        return MAKELONG(IDNO, DC_HASDEFID);
    case WM_TIMER:
        if (p->left > 0) {
            p->left--;
        }
        if (p->left == 0) {
            answer(w, p, PROMPT_TIMEOUT);
        } else {
            InvalidateRect(w, NULL, TRUE);
        }
        return 0;
    case WM_CLOSE:
        answer(w, p, PROMPT_NO);
        return 0;
    case WM_PROMPT_END:
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        KillTimer(w, TIMER_ID);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static HWND make_button(HWND parent, const char *text, int id, DWORD kind,
                        int x, int y, int cx, int cy)
{
    return CreateWindowExA(0, "BUTTON", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | kind,
                           x, y, cx, cy, parent, (HMENU) id,
                           GetModuleHandleA(NULL), NULL);
}

static HWND make_box(PROMPT *p)
{
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    DWORD exstyle = WS_EX_TOPMOST | WS_EX_DLGMODALFRAME;
    LONG units = GetDialogBaseUnits();
    int cx = LOWORD(units), cy = HIWORD(units);
    int m = cx * 2, tw = cx * 44, bw = cx * 10, bh = cy * 3 / 2 + 4;
    int by = m + cy * 4;
    WNDCLASSA wc;
    RECT r, yes;
    HWND w;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = box_proc;
    wc.cbWndExtra = sizeof(LONG);
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) (COLOR_BTNFACE + 1);
    wc.lpszClassName = CLASS_NAME;
    if (!RegisterClassA(&wc) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        log_msg(LOG_ERROR, "prompt: RegisterClass failed, error %lu",
                (unsigned long) GetLastError());
        return NULL;
    }
    p->margin = m;
    p->line = cy;
    r.left = 0;
    r.top = 0;
    r.right = tw + 2 * m;
    r.bottom = by + bh + m;
    AdjustWindowRectEx(&r, style, FALSE, exstyle);
    w = CreateWindowExA(exstyle, CLASS_NAME, "ntrdps - remote control",
                        style, 0, 0, r.right - r.left, r.bottom - r.top,
                        NULL, NULL, GetModuleHandleA(NULL), p);
    if (w == NULL) {
        log_msg(LOG_ERROR, "prompt: CreateWindowEx failed, error %lu",
                (unsigned long) GetLastError());
        return NULL;
    }
    if (make_button(w, "Yes", IDYES, BS_PUSHBUTTON, m + tw - 2 * bw - cx,
                    by, bw, bh) == NULL ||
        make_button(w, "No", IDNO, BS_DEFPUSHBUTTON, m + tw - bw, by, bw,
                    bh) == NULL ||
        SetTimer(w, TIMER_ID, 1000, NULL) == 0) {
        log_msg(LOG_ERROR, "prompt: a button or the timer failed, error "
                "%lu", (unsigned long) GetLastError());
        DestroyWindow(w);
        return NULL;
    }
    SetWindowPos(w, HWND_TOPMOST,
                 (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2,
                 (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 2,
                 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    MessageBeep(MB_ICONQUESTION);
    GetWindowRect(GetDlgItem(w, IDYES), &yes);
    log_msg(LOG_INFO, "prompt: \"%s\" on the console, %u s; the Yes button "
            "is at %ld,%ld on the screen", p->question, p->left,
            (yes.left + yes.right) / 2, (yes.top + yes.bottom) / 2);
    return w;
}

static int is_ended(PROMPT *p)
{
    int ended;

    EnterCriticalSection(&p->lock);
    ended = p->ended;
    LeaveCriticalSection(&p->lock);
    return ended;
}

static DWORD WINAPI prompt_thread(LPVOID arg)
{
    PROMPT *p = (PROMPT *) arg;
    DESKTOP_THREAD d;
    HWND w = NULL;
    MSG msg;
    int ended;

    memset(&d, 0, sizeof(d));
    d.who = "prompt";
    if (!desktop_enter(&d, "Default")) {
        p->answer = PROMPT_FAILED;
    } else if (!is_ended(p)) {
        w = make_box(p);
        if (w == NULL) {
            p->answer = PROMPT_FAILED;
        }
    }
    if (w != NULL) {
        EnterCriticalSection(&p->lock);
        p->wnd = w;
        ended = p->ended;
        LeaveCriticalSection(&p->lock);
        if (ended) {
            DestroyWindow(w);
        }
        while (GetMessageA(&msg, NULL, 0, 0) > 0) {
            if (!IsDialogMessageA(w, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
        }
        EnterCriticalSection(&p->lock);
        p->wnd = NULL;
        LeaveCriticalSection(&p->lock);
    }
    desktop_leave(&d);
    EnterCriticalSection(&p->lock);
    if (p->ended) {
        log_msg(LOG_INFO, "prompt: \"%s\" closed: the session ended "
                "before an answer", p->question);
    } else if (p->answer != 0) {
        p->done(p->ctx, p->answer);
    }
    LeaveCriticalSection(&p->lock);
    return 0;
}

void *prompt_ask(const char *address, unsigned int timeout_s,
                 PROMPT_DONE done, void *ctx)
{
    PROMPT *p = (PROMPT *) calloc(1, sizeof(PROMPT));
    DWORD tid;

    if (p == NULL) {
        log_msg(LOG_ERROR, "prompt: out of memory");
        return NULL;
    }
    sprintf(p->question, "Allow remote control from %.*s?",
            (int) (strcspn(address, ":") < 32 ? strcspn(address, ":") : 32),
            address);
    p->left = timeout_s;
    p->done = done;
    p->ctx = ctx;
    InitializeCriticalSection(&p->lock);
    p->thread = CreateThread(NULL, 0, prompt_thread, p, 0, &tid);
    if (p->thread == NULL) {
        log_msg(LOG_ERROR, "prompt: CreateThread failed, error %lu",
                (unsigned long) GetLastError());
        DeleteCriticalSection(&p->lock);
        free(p);
        return NULL;
    }
    return p;
}

void prompt_end(void *prompt)
{
    PROMPT *p = (PROMPT *) prompt;

    if (p == NULL) {
        return;
    }
    EnterCriticalSection(&p->lock);
    p->ended = 1;
    if (p->wnd != NULL) {
        PostMessageA(p->wnd, WM_PROMPT_END, 0, 0);
    }
    LeaveCriticalSection(&p->lock);
    if (WaitForSingleObject(p->thread, END_WAIT_MS) != WAIT_OBJECT_0) {
        log_msg(LOG_WARN, "prompt: the thread did not end within %d s",
                END_WAIT_MS / 1000);
        CloseHandle(p->thread);
        return;
    }
    CloseHandle(p->thread);
    DeleteCriticalSection(&p->lock);
    free(p);
}
