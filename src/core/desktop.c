/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <string.h>
#include "log.h"
#include "desktop.h"

#define OBJ_NAME     2
#define WINSTA_ALL   0x037FUL

#define DESK_ACCESS  0x00EFUL

#define NO_CODE      0xFFFFFFFFUL

typedef HANDLE (WINAPI *OPEN_WINSTA)(LPCSTR name, BOOL inherit,
                                     DWORD access);
typedef BOOL   (WINAPI *SET_PROCESS_WINSTA)(HANDLE winsta);
typedef HANDLE (WINAPI *OPEN_INPUT_DESKTOP)(DWORD flags, BOOL inherit,
                                            DWORD access);
typedef HANDLE (WINAPI *OPEN_DESKTOP)(LPCSTR name, DWORD flags,
                                      BOOL inherit, DWORD access);
typedef BOOL   (WINAPI *GET_OBJECT_INFO)(HANDLE obj, int index, PVOID info,
                                         DWORD len, LPDWORD need);
typedef BOOL   (WINAPI *SET_THREAD_DESKTOP)(HANDLE desk);
typedef HANDLE (WINAPI *GET_THREAD_DESKTOP)(DWORD thread_id);
typedef BOOL   (WINAPI *CLOSE_DESKTOP)(HANDLE desk);

typedef HANDLE (WINAPI *OPEN_WINSTA31)(LPCSTR name, DWORD access);
typedef HANDLE (WINAPI *OPEN_DESKTOP31)(LPCSTR name, DWORD access);
typedef HANDLE (WINAPI *GET_INPUT_DESKTOP31)(void);

typedef struct {
    const char *name;
    const char *open_as;
    HANDLE      h;
    DWORD       error;
} DESK31;

#define DESK31_COUNT   3
#define DESK31_RETRY   5000

static DESK31 g_desk31[DESK31_COUNT] = {
    { "Winlogon",     "Winlogon",     NULL, 0 },
    { "Default",      "default",      NULL, 0 },
    { "Screen-saver", "Screen-saver", NULL, 0 }
};
static OPEN_DESKTOP31      p_open31;
static GET_INPUT_DESKTOP31 p_input31;
static CRITICAL_SECTION    g_lock31;
static DWORD               g_tried31;
static int                 g_nt31;
static int                 g_own31 = 1;

static OPEN_INPUT_DESKTOP p_open_input;
static OPEN_DESKTOP       p_open;
static GET_OBJECT_INFO    p_object_info;
static SET_THREAD_DESKTOP p_set_thread;
static GET_THREAD_DESKTOP p_get_thread;
static CLOSE_DESKTOP      p_close;
static int                g_ok;
static HANDLE             g_home;
#ifdef NTRDPS_LOGIN
static CRITICAL_SECTION   g_own_lock;
static HANDLE             g_own;
static char               g_own_name[64];
#endif

static DWORD last_error(void)
{
    DWORD err = GetLastError();

    return err != 0 ? err : NO_CODE;
}

static int open31(int i)
{
    DESK31 *k = &g_desk31[i];
    DWORD err;

    if (k->h != NULL) {
        return 1;
    }
    k->h = p_open31(k->open_as, DESK_ACCESS);
    if (k->h != NULL) {
        k->error = 0;
        log_msg(LOG_INFO, "desktops: NT 3.1: desktop \"%s\" is open",
                k->open_as);
        return 1;
    }
    err = last_error();
    if (err != k->error) {
        k->error = err;
        if (err == NO_CODE) {
            log_msg(LOG_INFO, "desktops: NT 3.1: OpenDesktop(\"%s\") "
                    "failed (no error code)", k->open_as);
        } else {
            log_msg(LOG_INFO, "desktops: NT 3.1: OpenDesktop(\"%s\") "
                    "failed, error %lu%s", k->open_as, (unsigned long) err,
                    err == ERROR_ACCESS_DENIED
                    ? " (only LocalSystem may open it: the service)" : "");
        }
    }
    return 0;
}

static int input31(DWORD *err)
{
    HANDLE h;
    DWORD now;
    int i, more = 0;

    h = p_input31();
    if (h == NULL) {
        *err = last_error();
        EnterCriticalSection(&g_lock31);
        now = GetTickCount();
        if (now - g_tried31 >= DESK31_RETRY) {
            g_tried31 = now;
            for (i = 0; i < DESK31_COUNT; i++) {
                if (g_desk31[i].h == NULL && open31(i)) {
                    more = 1;
                }
            }
        }
        LeaveCriticalSection(&g_lock31);
        if (!more) {
            return -1;
        }
        h = p_input31();
        if (h == NULL) {
            *err = last_error();
            return -1;
        }
    }
    for (i = 0; i < DESK31_COUNT; i++) {
        if (g_desk31[i].h == h) {
            return i;
        }
    }
    *err = NO_CODE;
    return -1;
}

static int init31(HMODULE user32)
{
    OPEN_WINSTA31 p_open_winsta;
    SET_PROCESS_WINSTA p_set_winsta;
    HANDLE winsta, own;
    int i, n = 0;

    p_open_winsta = (OPEN_WINSTA31) GetProcAddress(user32,
                                                  "OpenWindowStationA");
    p_set_winsta = (SET_PROCESS_WINSTA) GetProcAddress(user32,
                                                 "SetProcessWindowStation");
    p_open31 = (OPEN_DESKTOP31) GetProcAddress(user32, "OpenDesktopA");
    p_input31 = (GET_INPUT_DESKTOP31) GetProcAddress(user32,
                                                    "GetInputDesktop");
    p_set_thread = (SET_THREAD_DESKTOP) GetProcAddress(user32,
                                                 "SetThreadDesktop");
    p_get_thread = (GET_THREAD_DESKTOP) GetProcAddress(user32,
                                                 "GetThreadDesktop");
    if (p_open_winsta == NULL || p_set_winsta == NULL || p_open31 == NULL ||
        p_input31 == NULL || p_set_thread == NULL || p_get_thread == NULL) {
        log_msg(LOG_ERROR, "desktops: NT 3.1: USER32.DLL lacks a desktop "
                "function");
        return 0;
    }
    winsta = p_open_winsta("WinSta0", WINSTA_ALL);
    if (winsta == NULL) {
        log_msg(LOG_WARN, "desktops: NT 3.1: OpenWindowStation(WinSta0) "
                "failed, error %lu", (unsigned long) GetLastError());
    } else if (!p_set_winsta(winsta)) {
        log_msg(LOG_WARN, "desktops: NT 3.1: SetProcessWindowStation"
                "(WinSta0) failed, error %lu",
                (unsigned long) GetLastError());
    } else {
        log_msg(LOG_INFO, "desktops: window station WinSta0");
    }
    InitializeCriticalSection(&g_lock31);
    g_tried31 = GetTickCount();
    for (i = 0; i < DESK31_COUNT; i++) {
        n += open31(i);
    }
    if (n == 0) {
        log_msg(LOG_ERROR, "desktops: NT 3.1: no desktop opens: the input "
                "desktop is not followed");
        return 0;
    }
    own = p_get_thread(GetCurrentThreadId());
    for (i = 0; i < DESK31_COUNT; i++) {
        if (own != NULL && g_desk31[i].h == own) {
            g_own31 = i;
            break;
        }
    }
    log_msg(LOG_INFO, "desktops: NT 3.1: this process started on desktop "
            "\"%s\"%s; the other desktops are read by helper processes "
            "(trap 18)", g_desk31[g_own31].open_as,
            i < DESK31_COUNT ? "" : " (assumed: GetThreadDesktop gave no "
            "handle of ours)");
    g_nt31 = 1;
    g_ok = 1;
    return 1;
}

static void home35(void)
{
    HANDLE h;

    h = p_open("Default", 0, FALSE, DESK_ACCESS);
    if (h == NULL) {
        log_msg(LOG_WARN, "desktops: OpenDesktop(Default) failed, error "
                "%lu: if the service started before the logon, sessions "
                "may crash (trap 20)", (unsigned long) GetLastError());
        return;
    }
    if (!p_set_thread(h)) {
        log_msg(LOG_WARN, "desktops: SetThreadDesktop(Default) failed, "
                "error %lu: if the service started before the logon, "
                "sessions may crash (trap 20)",
                (unsigned long) GetLastError());
        p_close(h);
        return;
    }
    g_home = h;
    GetDoubleClickTime();
    if (p_get_thread(GetCurrentThreadId()) == NULL) {
        log_msg(LOG_WARN, "desktops: this thread got no USER data on "
                "\"Default\", error %lu: if the service started before the "
                "logon, sessions may crash (trap 20)",
                (unsigned long) GetLastError());
        return;
    }
    log_msg(LOG_INFO, "desktops: the main thread stays on \"Default\": "
            "new threads get their USER data there (NT 3.5x, trap 20)");
}

int desktop_init(void)
{
    DWORD v = GetVersion();
    HMODULE user32;
    OPEN_WINSTA p_open_winsta;
    SET_PROCESS_WINSTA p_set_winsta;
    HANDLE winsta;

#ifdef NTRDPS_LOGIN
    InitializeCriticalSection(&g_own_lock);
#endif
    user32 = LoadLibraryA("USER32.DLL");
    if (user32 == NULL) {
        log_msg(LOG_ERROR, "desktops: USER32.DLL does not load, error %lu",
                (unsigned long) GetLastError());
        return 0;
    }
    if ((v & 0xFF) * 100 + ((v >> 8) & 0xFF) < 350) {
        return init31(user32);
    }
    p_open_winsta = (OPEN_WINSTA) GetProcAddress(user32,
                                                 "OpenWindowStationA");
    p_set_winsta = (SET_PROCESS_WINSTA) GetProcAddress(user32,
                                                 "SetProcessWindowStation");
    p_open_input = (OPEN_INPUT_DESKTOP) GetProcAddress(user32,
                                                 "OpenInputDesktop");
    p_open = (OPEN_DESKTOP) GetProcAddress(user32, "OpenDesktopA");
    p_object_info = (GET_OBJECT_INFO) GetProcAddress(user32,
                                                 "GetUserObjectInformationA");
    p_set_thread = (SET_THREAD_DESKTOP) GetProcAddress(user32,
                                                 "SetThreadDesktop");
    p_get_thread = (GET_THREAD_DESKTOP) GetProcAddress(user32,
                                                 "GetThreadDesktop");
    p_close = (CLOSE_DESKTOP) GetProcAddress(user32, "CloseDesktop");
    if (p_open_winsta == NULL || p_set_winsta == NULL ||
        p_open_input == NULL || p_open == NULL || p_object_info == NULL ||
        p_set_thread == NULL || p_get_thread == NULL || p_close == NULL) {
        log_msg(LOG_ERROR, "desktops: USER32.DLL lacks a desktop function");
        return 0;
    }
    winsta = p_open_winsta("WinSta0", FALSE, WINSTA_ALL);
    if (winsta == NULL) {
        log_msg(LOG_ERROR, "desktops: OpenWindowStation(WinSta0) failed, "
                "error %lu", (unsigned long) GetLastError());
        return 0;
    }
    if (!p_set_winsta(winsta)) {
        log_msg(LOG_ERROR, "desktops: SetProcessWindowStation(WinSta0) "
                "failed, error %lu", (unsigned long) GetLastError());
        return 0;
    }
    log_msg(LOG_INFO, "desktops: window station WinSta0");
    g_ok = 1;
    if ((v & 0xFF) * 100 + ((v >> 8) & 0xFF) < 400) {
        home35();
    }
    return 1;
}

static void failed(DESKTOP_THREAD *t, DWORD err, const char *what)
{
    const char *q = t->name[0] ? "\"" : "";
    const char *where = t->name[0] ? t->name : "its first desktop";

    if (err == t->error) {
        return;
    }
    t->error = err;
    if (err == ERROR_BUSY) {
        log_msg(LOG_INFO, "%s thread: %s: busy (logoff or shutdown); it "
                "stays on %s%s%s and tries again", t->who, what, q, where,
                q);
    } else if (err == NO_CODE) {
        log_msg(LOG_WARN, "%s thread: %s failed (no error code); it stays "
                "on %s%s%s and tries again", t->who, what, q, where, q);
    } else {
        log_msg(LOG_WARN, "%s thread: %s failed, error %lu; it stays on "
                "%s%s%s and tries again", t->who, what, (unsigned long) err,
                q, where, q);
    }
}

static int follow31(DESKTOP_THREAD *t)
{
    DESK31 *k;
    DWORD err = 0;
    int i;

    i = input31(&err);
    if (i < 0) {
        failed(t, err, "GetInputDesktop (NT 3.1: the input desktop is "
               "not one this process could open)");
        return 0;
    }
    k = &g_desk31[i];
    if (t->desk == k->h) {
        t->error = 0;
        return 0;
    }
    if (t->error == 0) {
        log_msg(LOG_DEBUG, "%s thread: the input desktop is now \"%s\"",
                t->who, k->name);
    }
    if (t->first == NULL) {
        t->first = p_get_thread(GetCurrentThreadId());
    }
    if (!p_set_thread(k->h)) {
        failed(t, last_error(), "SetThreadDesktop");
        return 0;
    }
    t->desk = k->h;
    t->error = 0;
    strcpy(t->name, k->name);
    log_msg(LOG_INFO, "%s thread: on desktop \"%s\"", t->who, t->name);
    return 1;
}

int desktop_follow(DESKTOP_THREAD *t)
{
    char name[sizeof(t->name)];
    HANDLE h;
    DWORD need;

    if (!g_ok) {
        return 0;
    }
    if (g_nt31) {
        return follow31(t);
    }
    h = p_open_input(0, FALSE, DESK_ACCESS);
    if (h == NULL) {
        failed(t, last_error(), "OpenInputDesktop");
        return 0;
    }
    memset(name, 0, sizeof(name));
    if (!p_object_info(h, OBJ_NAME, name, sizeof(name) - 1, &need)) {
        failed(t, last_error(), "the name of the input desktop");
        p_close(h);
        return 0;
    }
    if (t->desk != NULL && strcmp(name, t->name) == 0) {
        p_close(h);
        t->error = 0;
        return 0;
    }
    if (t->first == NULL) {
        t->first = p_get_thread(GetCurrentThreadId());
    }
    if (!p_set_thread(h)) {
        failed(t, last_error(), "SetThreadDesktop");
        p_close(h);
        return 0;
    }
    if (t->desk != NULL) {
        p_close(t->desk);
    }
    t->desk = h;
    t->error = 0;
    strcpy(t->name, name);
    log_msg(LOG_INFO, "%s thread: on desktop \"%s\"", t->who, t->name);
    return 1;
}

static int enter31(DESKTOP_THREAD *t, const char *name)
{
    DESK31 *k = NULL;
    int i, open;

    for (i = 0; i < DESK31_COUNT; i++) {
        if (lstrcmpiA(g_desk31[i].name, name) == 0) {
            k = &g_desk31[i];
            break;
        }
    }
    if (k == NULL) {
        failed(t, ERROR_FILE_NOT_FOUND, "OpenDesktop (NT 3.1: not one of "
               "Winlogon's three desktops)");
        return 0;
    }
    EnterCriticalSection(&g_lock31);
    open = open31(i);
    LeaveCriticalSection(&g_lock31);
    if (!open) {
        failed(t, k->error, "OpenDesktop");
        return 0;
    }
    if (t->first == NULL) {
        t->first = p_get_thread(GetCurrentThreadId());
    }
    if (!p_set_thread(k->h)) {
        failed(t, last_error(), "SetThreadDesktop");
        return 0;
    }
    t->desk = k->h;
    t->error = 0;
    strcpy(t->name, k->name);
    log_msg(LOG_INFO, "%s thread: on desktop \"%s\"", t->who, t->name);
    return 1;
}

int desktop_enter(DESKTOP_THREAD *t, const char *name)
{
    HANDLE h;

    if (!g_ok) {
        return 0;
    }
    if (g_nt31) {
        return enter31(t, name);
    }
    h = p_open(name, 0, FALSE, DESK_ACCESS);
    if (h == NULL) {
        failed(t, last_error(), "OpenDesktop");
        return 0;
    }
    if (t->first == NULL) {
        t->first = p_get_thread(GetCurrentThreadId());
    }
    if (!p_set_thread(h)) {
        failed(t, last_error(), "SetThreadDesktop");
        p_close(h);
        return 0;
    }
    if (t->desk != NULL) {
        p_close(t->desk);
    }
    t->desk = h;
    t->error = 0;
    strncpy(t->name, name, sizeof(t->name) - 1);
    log_msg(LOG_INFO, "%s thread: on desktop \"%s\"", t->who, t->name);
    return 1;
}

#ifdef NTRDPS_LOGIN
typedef HANDLE (WINAPI *CREATE_DESKTOP)(LPCSTR name, LPCSTR device,
                                        PVOID devmode, DWORD flags,
                                        DWORD access, PVOID sa);
typedef HANDLE (WINAPI *CREATE_DESKTOP31)(LPCSTR name, LPCSTR device,
                                          PVOID attr);

static DWORD g_attr31[5] = { 20, 0, 0, 0, 0 };

int desktop_enter_own(DESKTOP_THREAD *t, const char *name)
{
    HMODULE user32 = GetModuleHandleA("USER32.DLL");
    CREATE_DESKTOP31 p_create31;
    CREATE_DESKTOP p_create;
    const char *what = "CreateDesktop";
    DWORD err = 0;
    HANDLE h;

    if (!g_ok || user32 == NULL) {
        return 0;
    }
    EnterCriticalSection(&g_own_lock);
    if (g_own == NULL) {
        if (g_nt31) {
            p_create31 = (CREATE_DESKTOP31) GetProcAddress(user32,
                                                         "CreateDesktopA");
            g_own = p_create31 != NULL
                ? p_create31(name, "DISPLAY", g_attr31) : NULL;
            if (g_own != NULL) {
                what = "OpenDesktop";
                g_own = p_open31(name, MAXIMUM_ALLOWED);
            }
        } else {
            p_create = (CREATE_DESKTOP) GetProcAddress(user32,
                                                       "CreateDesktopA");
            g_own = p_create != NULL
                ? p_create(name, NULL, NULL, 0, MAXIMUM_ALLOWED, NULL)
                : NULL;
        }
        if (g_own == NULL) {
            err = last_error();
        } else {
            strncpy(g_own_name, name, sizeof(g_own_name) - 1);
            log_msg(LOG_INFO, "desktops: made our own desktop \"%s\"",
                    g_own_name);
        }
    }
    h = g_own;
    LeaveCriticalSection(&g_own_lock);
    if (h == NULL) {
        failed(t, err, what);
        return 0;
    }
    if (t->first == NULL) {
        t->first = p_get_thread(GetCurrentThreadId());
    }
    if (!p_set_thread(h)) {
        failed(t, last_error(), "SetThreadDesktop");
        return 0;
    }
    t->desk = h;
    t->keep = 1;
    t->error = 0;
    strncpy(t->name, g_own_name, sizeof(t->name) - 1);
    log_msg(LOG_INFO, "%s thread: on desktop \"%s\"", t->who, t->name);
    return 1;
}
#endif

void desktop_leave(DESKTOP_THREAD *t)
{
    if (!g_ok || t->desk == NULL) {
        return;
    }
#ifdef NTRDPS_LOGIN
    if (t->keep) {
        p_set_thread(t->first);
        t->desk = NULL;
        return;
    }
#endif
    if (t->first != NULL && p_set_thread(t->first) && !g_nt31) {
        p_close(t->desk);
    }
    t->desk = NULL;
}

int desktop_input_name(char *name, unsigned int size)
{
    HANDLE h;
    DWORD need;
    int ok;

    if (size == 0) {
        return 0;
    }
    memset(name, 0, size);
    if (!g_ok) {
        return 0;
    }
    if (g_nt31) {
        DWORD err;
        int i = input31(&err);

        if (i < 0) {
            return 0;
        }
        strncpy(name, g_desk31[i].name, size - 1);
        return 1;
    }
    h = p_open_input(0, FALSE, DESK_ACCESS);
    if (h == NULL) {
        return 0;
    }
    ok = p_object_info(h, OBJ_NAME, name, size - 1, &need) != 0;
    p_close(h);
    if (!ok) {
        name[0] = '\0';
    }
    return ok;
}

int desktop_nt31(void)
{
    return g_nt31;
}

int desktop_foreign(const char *name)
{
    return g_nt31 && name != NULL && name[0] != '\0' &&
           lstrcmpiA(name, g_desk31[g_own31].name) != 0;
}
