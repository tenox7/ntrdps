/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "config.h"
#include "net.h"
#include "core.h"
#include "desktop.h"
#include "input.h"
#include "sas.h"
#include "session.h"
#include "rdp.h"
#include "version.h"
#include "mrinst.h"
#include "reader.h"

#define SERVICE_NAME "ntrdps"
#define DISPLAY_NAME "ntrdps RDP console server"
#define STOP_EVENT   "ntrdps_stop"
#define HOOK_DLL     "ntrdpshk.dll"
#define STOP_WAIT_S  10

#ifndef SERVICE_INTERACTIVE_PROCESS
#define SERVICE_INTERACTIVE_PROCESS 0x00000100
#endif

static HANDLE                g_stop = NULL;
static SERVICE_STATUS_HANDLE g_status_handle;
static SERVICE_STATUS        g_status;
static CONFIG                g_cfg;

static int nt_version(void)
{
    DWORD v = GetVersion();

    return (int) (v & 0xFF) * 100 + (int) ((v >> 8) & 0xFF);
}

static unsigned long nt_build(void)
{
    return (unsigned long) ((GetVersion() >> 16) & 0x7FFF);
}

static void print_error(const char *what)
{
    DWORD err = GetLastError();
    char text[256];
    DWORD n;

    n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, text,
                       sizeof(text), NULL);
    while (n > 0 && (text[n - 1] == '\r' || text[n - 1] == '\n' ||
                     text[n - 1] == ' ')) {
        n--;
    }
    text[n] = '\0';
    printf("ntrdps: %s failed, error %lu%s%s\n", what, (unsigned long) err,
           n > 0 ? ": " : "", text);
}

static int open_log(int to_console)
{
    DWORD err;

    if (g_cfg.log_file[0] == '\0') {
        return log_open(NULL, to_console);
    }
    if (log_open(g_cfg.log_file, to_console)) {
        return 1;
    }
    err = GetLastError();
    if (!log_open(NULL, to_console)) {
        return 0;
    }
    log_msg(LOG_WARN, "settings: LogFile \"%s\" does not open, error %lu: "
            "the log is %s", g_cfg.log_file, (unsigned long) err, log_path());
    return 1;
}

static int run(const char *mode, const CORE_BACKEND *backend)
{
    int v = nt_version();
    void *core;
    int rc;

    log_msg(LOG_INFO, "ntrdps %s started (%s mode), NT %d.%d build %lu",
            NTRDPS_VERSION, mode, v / 100, v % 100, nt_build());
    config_log(&g_cfg);
    if (log_get_level() != (int) g_cfg.value[CFG_LOG_LEVEL]) {
        log_msg(LOG_INFO, "log level %d (%s mode), not the LogLevel setting",
                log_get_level(), mode);
    }
    desktop_init();
    sas_start();
    input_start((int) g_cfg.value[CFG_ALTGR_FAKE_CTRL]);
    core = core_start(backend);
    core_set_prompt(g_cfg.value[CFG_CONSOLE_PROMPT] != 0
                    ? (unsigned int) g_cfg.value[CFG_PROMPT_TIMEOUT] : 0);
    core_set_new_client((int) g_cfg.value[CFG_NEW_CLIENT]);
    rdp_set_compression((int) g_cfg.value[CFG_COMPRESSION],
                        (int) g_cfg.value[CFG_COMPRESSION_MAX]);
#ifdef NTRDPS_TLS
    rdp_set_allow_tls((int) g_cfg.value[CFG_ALLOW_TLS]);
#endif
    rdp_start(&core_api, core);
    rc = net_serve((unsigned int) g_cfg.value[CFG_PORT], g_stop,
                   session_run);
    core_stop(core);
    input_stop();
    sas_stop();
    log_msg(LOG_INFO, "ntrdps stopped");
    return rc;
}

static void report_status(DWORD state, DWORD wait_hint)
{
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = state;
    g_status.dwControlsAccepted = (state == SERVICE_RUNNING)
        ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    g_status.dwWin32ExitCode = NO_ERROR;
    g_status.dwServiceSpecificExitCode = 0;
    if (state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING) {
        g_status.dwCheckPoint++;
    } else {
        g_status.dwCheckPoint = 0;
    }
    g_status.dwWaitHint = wait_hint;
    SetServiceStatus(g_status_handle, &g_status);
}

static VOID WINAPI service_ctrl(DWORD control)
{
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        log_msg(LOG_INFO, "service control %lu: stopping",
                (unsigned long) control);
        report_status(SERVICE_STOP_PENDING, 5000);
        SetEvent(g_stop);
        break;
    default:
        SetServiceStatus(g_status_handle, &g_status);
        break;
    }
}

static VOID WINAPI service_main(DWORD argc, LPSTR *argv)
{
    g_status_handle = RegisterServiceCtrlHandlerA(SERVICE_NAME,
                          (LPHANDLER_FUNCTION) service_ctrl);
    if (g_status_handle == 0) {
        log_msg(LOG_ERROR, "RegisterServiceCtrlHandler failed, error %lu",
                (unsigned long) GetLastError());
        return;
    }
    report_status(SERVICE_START_PENDING, 3000);
    g_stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (g_stop == NULL) {
        log_msg(LOG_ERROR, "CreateEvent failed, error %lu",
                (unsigned long) GetLastError());
        report_status(SERVICE_STOPPED, 0);
        return;
    }
    report_status(SERVICE_RUNNING, 0);
    run("service", &poll_backend);
    report_status(SERVICE_STOPPED, 0);
}

static int service_dispatch(void)
{
    SERVICE_TABLE_ENTRYA table[2];

    config_read(&g_cfg, HKEY_LOCAL_MACHINE, CONFIG_KEY);
    open_log(0);
    log_set_level((int) g_cfg.value[CFG_LOG_LEVEL]);
    table[0].lpServiceName = SERVICE_NAME;
    table[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTIONA) service_main;
    table[1].lpServiceName = NULL;
    table[1].lpServiceProc = NULL;
    if (!StartServiceCtrlDispatcherA(table)) {
        log_msg(LOG_ERROR, "StartServiceCtrlDispatcher failed, error %lu",
                (unsigned long) GetLastError());
        log_close();
        return 1;
    }
    log_close();
    return 0;
}

static BOOL WINAPI console_ctrl(DWORD type)
{
    log_msg(LOG_INFO, "console event %lu: stopping", (unsigned long) type);
    SetEvent(g_stop);
    return TRUE;
}

static int debug_run(const CORE_BACKEND *backend)
{
    int rc;

    g_stop = CreateEventA(NULL, TRUE, FALSE, STOP_EVENT);
    if (g_stop == NULL) {
        print_error("CreateEvent " STOP_EVENT);
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        printf("ntrdps: another \"ntrdps -debug\" is running\n");
        return 1;
    }
    config_read(&g_cfg, HKEY_LOCAL_MACHINE, CONFIG_KEY);
    if (!open_log(1)) {
        printf("ntrdps: cannot open the log file, error %lu; "
               "logging to the console only\n",
               (unsigned long) GetLastError());
    }
    log_set_level(LOG_DEBUG);
    SetConsoleCtrlHandler(console_ctrl, TRUE);
    rc = run("debug", backend);
    log_close();
    return rc;
}

static int debug_stop(void)
{
    HANDLE ev = OpenEventA(EVENT_MODIFY_STATE, FALSE, STOP_EVENT);

    if (ev == NULL) {
        printf("ntrdps: no \"ntrdps -debug\" is running (error %lu)\n",
               (unsigned long) GetLastError());
        return 1;
    }
    SetEvent(ev);
    CloseHandle(ev);
    printf("ntrdps: stop sent\n");
    return 0;
}

static int exe_path(char *exe)
{
    DWORD n = GetModuleFileNameA(NULL, exe, MAX_PATH);

    if (n == 0 || n >= MAX_PATH) {
        print_error("GetModuleFileName");
        return 0;
    }
    exe[n] = '\0';
    return 1;
}

static int on_network(const char *path)
{
    char root[4];

    if (path[0] == '\\' && path[1] == '\\') {
        return 1;
    }
    if (path[0] == '\0' || path[1] != ':') {
        return 0;
    }
    root[0] = path[0];
    root[1] = ':';
    root[2] = '\\';
    root[3] = '\0';
    return GetDriveTypeA(root) == DRIVE_REMOTE;
}

static void scm_error(void)
{
    if (GetLastError() == ERROR_ACCESS_DENIED) {
        printf("ntrdps: access denied. Only a member of the Administrators "
               "group can\ninstall or remove a service.\n");
    } else {
        print_error("OpenSCManager");
    }
}

static int install(void)
{
    char exe[MAX_PATH];
    char cmd[MAX_PATH + 16];
    char dll[MAX_PATH + sizeof(HOOK_DLL)];
    char *slash;
    SC_HANDLE scm, svc;
    DWORD type = SERVICE_WIN32_OWN_PROCESS;
    DWORD err;

    if (!exe_path(exe)) {
        return 1;
    }
    if (on_network(exe)) {
        printf("ntrdps: %s is on a network drive.\nThe service cannot "
               "start from there. Copy ntrdps.exe and " HOOK_DLL "\nto a "
               "local disk (for example C:\\NTRDPS) and run \"ntrdps "
               "-install\" there.\n", exe);
        return 1;
    }
    strcpy(dll, exe);
    slash = strrchr(dll, '\\');
    strcpy(slash != NULL ? slash + 1 : dll, HOOK_DLL);
    if (GetFileAttributesA(dll) == 0xFFFFFFFFUL) {
        printf("ntrdps: warning: %s is missing.\nWithout it, clients "
               "show only their default arrow pointer, and screen\nchanges "
               "come later. Copy it next to ntrdps.exe.\n", dll);
    }
    if (strchr(exe, ' ') != NULL) {
        sprintf(cmd, "\"%s\" -service", exe);
    } else {
        sprintf(cmd, "%s -service", exe);
    }
    if (nt_version() >= 350) {
        type |= SERVICE_INTERACTIVE_PROCESS;
    }
    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (scm == NULL) {
        scm_error();
        return 1;
    }
    svc = CreateServiceA(scm, SERVICE_NAME, DISPLAY_NAME, SERVICE_ALL_ACCESS,
                         type, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                         cmd, NULL, NULL, NULL, NULL, NULL);
    if (svc == NULL) {
        err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS) {
            printf("ntrdps: the service \"%s\" is already installed. To "
                   "install this copy\ninstead: \"ntrdps -uninstall\", "
                   "then \"ntrdps -install\".\n", SERVICE_NAME);
        } else if (err == ERROR_SERVICE_MARKED_FOR_DELETE) {
            printf("ntrdps: the old service \"%s\" is still being "
                   "removed. Close the Services\nwindow of the Control "
                   "Panel (or reboot), then try again.\n", SERVICE_NAME);
        } else {
            print_error("CreateService");
        }
        CloseServiceHandle(scm);
        return 1;
    }
    printf("ntrdps: service \"%s\" installed.\n"
           "  Program:  %s\n"
           "  Starts:   at every boot, as LocalSystem\n"
           "  Settings: HKEY_LOCAL_MACHINE\\%s (read at start)\n"
           "Start it now with: net start %s\n",
           SERVICE_NAME, cmd, CONFIG_KEY, SERVICE_NAME);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 0;
}

static int remove_service(void)
{
    SC_HANDLE scm, svc;
    SERVICE_STATUS st;
    DWORD err;
    int i, rc = 0;

    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm == NULL) {
        scm_error();
        return 1;
    }
    svc = OpenServiceA(scm, SERVICE_NAME,
                       SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
    if (svc == NULL) {
        err = GetLastError();
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            printf("ntrdps: the service \"%s\" is not installed\n",
                   SERVICE_NAME);
        } else if (err == ERROR_ACCESS_DENIED) {
            SetLastError(err);
            scm_error();
            rc = 1;
        } else {
            print_error("OpenService " SERVICE_NAME);
            rc = 1;
        }
        CloseServiceHandle(scm);
        return rc;
    }
    if (ControlService(svc, SERVICE_CONTROL_STOP, &st)) {
        printf("ntrdps: stopping the service\n");
        for (i = 0; i < STOP_WAIT_S * 2 &&
                    st.dwCurrentState != SERVICE_STOPPED; i++) {
            Sleep(500);
            if (!QueryServiceStatus(svc, &st)) {
                break;
            }
        }
        if (st.dwCurrentState != SERVICE_STOPPED) {
            printf("ntrdps: the service did not stop within %d s; it goes "
                   "away when it stops\n", STOP_WAIT_S);
        }
    } else if (GetLastError() != ERROR_SERVICE_NOT_ACTIVE) {
        print_error("stopping the service");
    }
    if (DeleteService(svc)) {
        printf("ntrdps: service \"%s\" removed\n", SERVICE_NAME);
    } else if (GetLastError() == ERROR_SERVICE_MARKED_FOR_DELETE) {
        printf("ntrdps: the service \"%s\" is already being removed; it "
               "goes away when\nthe Services window closes (or at the "
               "next boot)\n", SERVICE_NAME);
    } else {
        print_error("DeleteService");
        rc = 1;
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return rc;
}

static int uninstall(void)
{
    char exe[MAX_PATH];
    char *slash;
    LONG err;

    if (remove_service() != 0) {
        return 1;
    }
    err = RegDeleteKeyA(HKEY_LOCAL_MACHINE, CONFIG_KEY);
    if (err == ERROR_SUCCESS) {
        printf("ntrdps: settings removed (HKEY_LOCAL_MACHINE\\%s)\n",
               CONFIG_KEY);
    } else if (err == ERROR_FILE_NOT_FOUND) {
        printf("ntrdps: no settings to remove (HKEY_LOCAL_MACHINE\\%s)\n",
               CONFIG_KEY);
    } else {
        SetLastError((DWORD) err);
        print_error("deleting HKEY_LOCAL_MACHINE\\" CONFIG_KEY);
        return 1;
    }
    if (exe_path(exe)) {
        slash = strrchr(exe, '\\');
        if (slash != NULL) {
            slash[1] = '\0';
        }
        printf("ntrdps: the files stay. Delete the folder %s by hand.\n"
               "A file hk*.dll that a program still uses: delete it after "
               "the next boot.\n", exe);
    }
    return 0;
}

static int usage(void)
{
    printf("ntrdps %s - RDP console server for Windows NT\n"
           "usage: ntrdps -install | -uninstall | -stop\n"
           "       ntrdps -installmirror | -uninstallmirror\n"
           "       ntrdps -installlayer | -uninstalllayer\n"
           "       ntrdps -debug [-testpicture | -getdibits | -nohints |\n"
           "                      -nomirror | -shadowcheck | -noorders |\n"
           "                      -notext | -nobitmaps | -nopatterns]\n"
           "  -install      create the service \"ntrdps\" for this "
           "ntrdps.exe\n"
           "  -uninstall    stop and delete the service and its settings\n"
           "  -debug        run in this console (Ctrl+C or \"ntrdps -stop\" "
           "ends it)\n"
           "  -testpicture  with -debug: show a test picture, not the "
           "console\n"
           "  -getdibits    with -debug: read the screen the NT 3.1 way\n"
           "  -nohints      with -debug: no change hints, sample every tick\n"
           "  -nomirror     with -debug: do not use the mirror driver\n"
           "  -shadowcheck  with -debug: compare the screen with the "
           "mirror's shadow\n"
           "  -noorders     with -debug: no drawing orders from the mirror\n"
           "  -notext       with -debug: no text orders from the mirror\n"
           "  -nobitmaps    with -debug: no bitmap orders from the mirror\n"
           "  -nopatterns   with -debug: no pattern brush orders from the "
           "mirror\n"
           "  -stop         end a running \"ntrdps -debug\"\n"
           "  -installmirror    NT 4.0: install the mirror driver "
           "(then reboot)\n"
           "  -uninstallmirror  switch the mirror driver off (then reboot)\n"
           "  -installlayer     NT 3.1 - 3.51: install the layered driver "
           "(then reboot)\n"
           "  -uninstalllayer   switch the layered driver off (then reboot)\n"
           "Settings: registry key HKLM\\" CONFIG_KEY ", read at start.\n",
           NTRDPS_VERSION);
    return 2;
}

int main(int argc, char **argv)
{
    if (argc == 5 && strcmp(argv[1], "-reader") == 0) {
        return reader_main(argv[2], argv[3], argv[4]);
    }
    if (argc == 3 && strcmp(argv[1], "-debug") == 0) {
        if (strcmp(argv[2], "-testpicture") == 0) {
            return debug_run(&testpat_backend);
        }
        if (strcmp(argv[2], "-getdibits") == 0) {
            poll_set_getdibits(1);
            return debug_run(&poll_backend);
        }
        if (strcmp(argv[2], "-nohints") == 0) {
            poll_set_nohints(1);
            return debug_run(&poll_backend);
        }
        if (strcmp(argv[2], "-nomirror") == 0) {
            poll_set_nomirror(1);
            return debug_run(&poll_backend);
        }
        if (strcmp(argv[2], "-shadowcheck") == 0) {
            poll_set_shadowcheck(1);
            return debug_run(&poll_backend);
        }
        if (strcmp(argv[2], "-noorders") == 0) {
            poll_set_noorders(1);
            return debug_run(&poll_backend);
        }
        if (strcmp(argv[2], "-notext") == 0) {
            poll_set_notext(1);
            return debug_run(&poll_backend);
        }
        if (strcmp(argv[2], "-nobitmaps") == 0) {
            poll_set_nobitmaps(1);
            return debug_run(&poll_backend);
        }
        if (strcmp(argv[2], "-nopatterns") == 0) {
            poll_set_nopatterns(1);
            return debug_run(&poll_backend);
        }
        return usage();
    }
    if (argc != 2) {
        return usage();
    }
    if (strcmp(argv[1], "-service") == 0) {
        return service_dispatch();
    }
    if (strcmp(argv[1], "-debug") == 0) {
        return debug_run(&poll_backend);
    }
    if (strcmp(argv[1], "-stop") == 0) {
        return debug_stop();
    }
    if (strcmp(argv[1], "-install") == 0) {
        return install();
    }
    if (strcmp(argv[1], "-uninstall") == 0) {
        return uninstall();
    }
    if (strcmp(argv[1], "-installmirror") == 0) {
        return mirror_install();
    }
    if (strcmp(argv[1], "-uninstallmirror") == 0) {
        return mirror_uninstall();
    }
    if (strcmp(argv[1], "-installlayer") == 0) {
        return layer_install();
    }
    if (strcmp(argv[1], "-uninstalllayer") == 0) {
        return layer_uninstall();
    }
    return usage();
}
