/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <string.h>
#include "log.h"
#include "desktop.h"
#include "sas.h"

#define STOP_WAIT_MS  5000
#define SAS_HOTKEY_ID 0

#ifndef WM_HOTKEY
#define WM_HOTKEY     0x0312
#endif
#ifndef MOD_ALT
#define MOD_ALT       0x0001
#endif
#ifndef MOD_CONTROL
#define MOD_CONTROL   0x0002
#endif

typedef char sas_hotkey_id_is_0[SAS_HOTKEY_ID == 0 ? 1 : -1];

static HANDLE g_thread;
static HANDLE g_stop;
static HANDLE g_go;

static void send_sas(DESKTOP_THREAD *d)
{
    HWND w;

    if (d->desk == NULL) {
        desktop_enter(d, "Winlogon");
    }
    w = FindWindowA("SAS window class", "SAS window");
    if (w == NULL) {
        log_msg(LOG_WARN, "SAS: Ctrl+Alt+Del not sent: Winlogon's \"SAS "
                "window\" was not found%s", d->desk == NULL
                ? " (this thread is not on the Winlogon desktop: only the "
                  "service, as LocalSystem, may open it)" : "");
        return;
    }
    if (!PostMessageA(w, WM_HOTKEY, SAS_HOTKEY_ID,
                      MAKELONG(MOD_CONTROL | MOD_ALT, VK_DELETE))) {
        log_msg(LOG_WARN, "SAS: Ctrl+Alt+Del not sent: PostMessage failed, "
                "error %lu", (unsigned long) GetLastError());
        return;
    }
    log_msg(LOG_INFO, "SAS: Ctrl+Alt+Del sent to Winlogon");
}

static DWORD WINAPI sas_thread(LPVOID arg)
{
    DESKTOP_THREAD d;
    HANDLE h[2];
    DWORD r;

    memset(&d, 0, sizeof(d));
    d.who = "SAS";
    h[0] = g_stop;
    h[1] = g_go;
    for (;;) {
        r = WaitForMultipleObjects(2, h, FALSE, INFINITE);
        if (r != WAIT_OBJECT_0 + 1) {
            if (r != WAIT_OBJECT_0) {
                log_msg(LOG_ERROR, "SAS: WaitForMultipleObjects failed, "
                        "error %lu: no more Ctrl+Alt+Del",
                        (unsigned long) GetLastError());
            }
            break;
        }
        send_sas(&d);
    }
    desktop_leave(&d);
    return 0;
}

int sas_start(void)
{
    DWORD tid;

    g_stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    g_go = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (g_stop == NULL || g_go == NULL) {
        log_msg(LOG_ERROR, "SAS: CreateEvent failed, error %lu: no "
                "Ctrl+Alt+Del", (unsigned long) GetLastError());
        sas_stop();
        return 0;
    }
    g_thread = CreateThread(NULL, 0, sas_thread, NULL, 0, &tid);
    if (g_thread == NULL) {
        log_msg(LOG_ERROR, "SAS: CreateThread failed, error %lu: no "
                "Ctrl+Alt+Del", (unsigned long) GetLastError());
        sas_stop();
        return 0;
    }
    return 1;
}

void sas_stop(void)
{
    if (g_thread != NULL) {
        SetEvent(g_stop);
        if (WaitForSingleObject(g_thread, STOP_WAIT_MS) != WAIT_OBJECT_0) {
            log_msg(LOG_WARN, "SAS: the thread did not end within %d s",
                    STOP_WAIT_MS / 1000);
        }
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    if (g_stop != NULL) {
        CloseHandle(g_stop);
        g_stop = NULL;
    }
    if (g_go != NULL) {
        CloseHandle(g_go);
        g_go = NULL;
    }
}

void sas_send(void)
{
    if (g_thread != NULL) {
        SetEvent(g_go);
    }
}
