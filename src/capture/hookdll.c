/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "desktop.h"
#include "hookdll.h"

#define HOOK_DLL      "ntrdpshk.dll"
#define COPY_NAME     "hk%06lx.dll"
#define COPY_FIND     "hk*.dll"
#define COPY_LEN      12
#define READY_WAIT_MS 5000
#define STOP_WAIT_MS  5000

typedef BOOL    (WINAPI *HOOK_START)(void);
typedef BOOL    (WINAPI *HOOK_STOP)(void);
typedef HCURSOR (WINAPI *HOOK_CURSOR)(LONG *serial, DWORD *thread);
typedef int     (WINAPI *HOOK_HINTS)(RECT *out, int max, int *all);

typedef struct {
    HMODULE      dll;
    HOOK_START   hook_start;
    HOOK_STOP    hook_stop;
    HOOK_CURSOR  hook_cursor;
    HOOK_HINTS   hook_hints;
    HANDLE       thread, stop, ready;
    volatile int hooked;
} HOOKDLL;

static HOOKDLL g_dll;

static DWORD WINAPI hook_thread(LPVOID arg)
{
    HOOKDLL *k = (HOOKDLL *) arg;
    DESKTOP_THREAD d;

    memset(&d, 0, sizeof(d));
    d.who = "hook";
    desktop_enter(&d, "Default");
    if (k->hook_start()) {
        k->hooked = 1;
    } else {
        log_msg(LOG_WARN, "hooks: SetWindowsHookEx failed, error %lu: "
                "clients show their default arrow, and the screen is "
                "polled without hints", (unsigned long) GetLastError());
    }
    SetEvent(k->ready);
    if (k->hooked) {
        WaitForSingleObject(k->stop, INFINITE);
        k->hooked = 0;
        if (!k->hook_stop()) {
            log_msg(LOG_WARN, "hooks: UnhookWindowsHookEx failed, error %lu",
                    (unsigned long) GetLastError());
        }
    }
    desktop_leave(&d);
    return 0;
}

static void release(HOOKDLL *k)
{
    if (k->thread != NULL) {
        CloseHandle(k->thread);
    }
    if (k->stop != NULL) {
        CloseHandle(k->stop);
    }
    if (k->ready != NULL) {
        CloseHandle(k->ready);
    }
    if (k->dll != NULL) {
        FreeLibrary(k->dll);
    }
    memset(k, 0, sizeof(*k));
}

static void failed(HOOKDLL *k, const char *what, const char *path)
{
    log_msg(LOG_WARN, "hooks: %s%s%s, error %lu: clients show their "
            "default arrow, and the screen is polled without hints", what,
            path != NULL ? " " : "", path != NULL ? path : "",
            (unsigned long) GetLastError());
    release(k);
}

static int file_id(const char *path, DWORD *id)
{
    unsigned char buf[4096];
    DWORD n, i, h = 2166136261UL;
    HANDLE f;
    int ok;

    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        return 0;
    }
    while ((ok = ReadFile(f, buf, sizeof(buf), &n, NULL)) != 0 && n > 0) {
        for (i = 0; i < n; i++) {
            h = (h ^ buf[i]) * 16777619UL;
        }
    }
    CloseHandle(f);
    *id = ((h >> 24) ^ h) & 0xFFFFFFUL;
    return ok;
}

static int copy_name(const char *name)
{
    int i;

    if (strlen(name) != COPY_LEN || (name[0] != 'h' && name[0] != 'H') ||
        (name[1] != 'k' && name[1] != 'K') ||
        lstrcmpiA(name + 8, ".dll") != 0) {
        return 0;
    }
    for (i = 2; i < 8; i++) {
        if (strchr("0123456789abcdefABCDEF", name[i]) == NULL) {
            return 0;
        }
    }
    return 1;
}

static void dll_path(char *path, size_t dir)
{
    char src[MAX_PATH + sizeof(HOOK_DLL)];
    char name[COPY_LEN + 1];
    WIN32_FIND_DATAA f;
    HANDLE find;
    DWORD id;

    memcpy(src, path, dir);
    strcpy(src + dir, HOOK_DLL);
    if (!file_id(src, &id)) {
        strcpy(path, src);
        return;
    }
    sprintf(name, COPY_NAME, (unsigned long) id);
    strcpy(path + dir, COPY_FIND);
    find = FindFirstFileA(path, &f);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (copy_name(f.cFileName) && lstrcmpiA(f.cFileName, name) != 0) {
                strcpy(path + dir, f.cFileName);
                DeleteFileA(path);
            }
        } while (FindNextFileA(find, &f));
        FindClose(find);
    }
    strcpy(path + dir, name);
    if (GetFileAttributesA(path) == 0xFFFFFFFFUL &&
        !CopyFileA(src, path, TRUE)) {
        log_msg(LOG_WARN, "hooks: cannot copy %s to %s, error %lu: the "
                "hooks use %s itself; replacing it while programs hold it "
                "can crash them", src, path, (unsigned long) GetLastError(),
                HOOK_DLL);
        strcpy(path, src);
    }
}

void hookdll_start(void)
{
    HOOKDLL *k = &g_dll;
    char path[MAX_PATH + sizeof(HOOK_DLL)];
    char *slash = NULL;
    DWORD n, tid;

    memset(k, 0, sizeof(*k));
    n = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        path[n] = '\0';
        slash = strrchr(path, '\\');
    }
    if (slash == NULL) {
        failed(k, "no folder of ntrdps.exe", NULL);
        return;
    }
    dll_path(path, (size_t) (slash + 1 - path));
    k->dll = LoadLibraryA(path);
    if (k->dll == NULL) {
        failed(k, "cannot load", path);
        return;
    }
    k->hook_start = (HOOK_START) GetProcAddress(k->dll, "HookStart");
    k->hook_stop = (HOOK_STOP) GetProcAddress(k->dll, "HookStop");
    k->hook_cursor = (HOOK_CURSOR) GetProcAddress(k->dll, "HookCursor");
    k->hook_hints = (HOOK_HINTS) GetProcAddress(k->dll, "HookHints");
    if (k->hook_start == NULL || k->hook_stop == NULL ||
        k->hook_cursor == NULL || k->hook_hints == NULL) {
        failed(k, "an export is missing (an old copy?) in", path);
        return;
    }
    k->stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    k->ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (k->stop == NULL || k->ready == NULL) {
        failed(k, "CreateEvent failed", NULL);
        return;
    }
    k->thread = CreateThread(NULL, 0, hook_thread, k, 0, &tid);
    if (k->thread == NULL) {
        failed(k, "CreateThread failed", NULL);
        return;
    }
    if (WaitForSingleObject(k->ready, READY_WAIT_MS) != WAIT_OBJECT_0) {
        log_msg(LOG_WARN, "hooks: the hook thread did not answer within "
                "%d s", READY_WAIT_MS / 1000);
    }
    if (k->hooked) {
        log_msg(LOG_INFO, "hooks: the hooks of %s are in place", path);
    }
}

void hookdll_stop(void)
{
    HOOKDLL *k = &g_dll;

    if (k->thread != NULL) {
        SetEvent(k->stop);
        if (WaitForSingleObject(k->thread, STOP_WAIT_MS) != WAIT_OBJECT_0) {
            log_msg(LOG_ERROR, "hooks: the hook thread did not end within "
                    "%d s", STOP_WAIT_MS / 1000);
            return;
        }
    }
    release(k);
}

int hookdll_ready(void)
{
    return g_dll.hooked;
}

HCURSOR hookdll_cursor(LONG *serial, DWORD *thread)
{
    return g_dll.hook_cursor(serial, thread);
}

int hookdll_hints(RECT *out, int max, int *all)
{
    return g_dll.hook_hints(out, max, all);
}
