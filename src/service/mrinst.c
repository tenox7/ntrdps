/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "mrinst.h"

#define MR_SERVICE  "ntrdpsmr"
#define MR_DISPLAY  "ntrdps mirror driver"
#define MR_SYS      "ntrdpsmr.sys"
#define MR_DLL      "ntrdpsmr.dll"
#define MR_DLL0     "ntrdpsm0.dll"
#define MR_IMAGE    "System32\\drivers\\" MR_SYS
#define MR_SVCKEY   "SYSTEM\\CurrentControlSet\\Services\\" MR_SERVICE
#define MR_DEVKEY   MR_SVCKEY "\\Device0"
#define CSD_KEY     "SYSTEM\\CurrentControlSet\\Control\\Windows"

#define LY_DLL      "ntrdpsly.dll"
#define LY_FILE     "layrdisp.dll"
#define LY_OFF      "layrdisp.off"
#define LY_KEY      "SYSTEM\\CurrentControlSet\\Control\\ntrdps"
#define LY_VALUE    "LayerDriver"
#define LY_MARK     "ntrdpsly: ntrdps layered display driver"
#define LY31_KEY    "SYSTEM\\CurrentControlSet\\Control\\DisplayDriverControl"
#define LY31_NAME   "layrdisp"

static const struct {
    const char *name;
    DWORD       value;
} g_values[] = {
    { "VgaCompatible",      0 },
    { "MirrorDriver",       1 },
    { "Attached.ToDesktop", 1 },
    { "MirroringDriver",    1 },
    { "Attach.ToDesktop",   1 }
};
#define N_VALUES (sizeof(g_values) / sizeof(g_values[0]))

static void mi_error(const char *what, DWORD err)
{
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

static int sp_level(void)
{
    HKEY k;
    DWORD type = 0, v = 0, cb = sizeof(v);
    LONG e;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, CSD_KEY, 0, KEY_QUERY_VALUE,
                      &k) != ERROR_SUCCESS) {
        return 0;
    }
    e = RegQueryValueExA(k, "CSDVersion", NULL, &type, (BYTE *) &v, &cb);
    RegCloseKey(k);
    if (e != ERROR_SUCCESS || type != REG_DWORD) {
        return 0;
    }
    return (int) ((v >> 8) & 0xFF);
}

static int move_aside(const char *dst, char *old, size_t cb)
{
    int i;

    if (strlen(dst) + 5 >= cb) {
        return 0;
    }
    for (i = 0; i <= 9; i++) {
        if (i == 0) {
            sprintf(old, "%s.old", dst);
        } else {
            sprintf(old, "%s.old%d", dst, i);
        }
        DeleteFileA(old);
        if (MoveFileA(dst, old)) {
            return 1;
        }
    }
    return 0;
}

static int copy_file(const char *name, const char *dir, const char *dstname,
                     const char *off)
{
    char src[MAX_PATH], dst[MAX_PATH], old[MAX_PATH];
    char *slash;
    DWORD n, err;

    n = GetModuleFileNameA(NULL, src, sizeof(src));
    if (n == 0 || n >= sizeof(src) - strlen(name)) {
        mi_error("GetModuleFileName", GetLastError());
        return 0;
    }
    slash = strrchr(src, '\\');
    strcpy(slash != NULL ? slash + 1 : src, name);
    if (strlen(dir) + 1 + strlen(dstname) >= sizeof(dst)) {
        printf("ntrdps: the folder name %s is too long\n", dir);
        return 0;
    }
    sprintf(dst, "%s\\%s", dir, dstname);
    if (GetFileAttributesA(src) == 0xFFFFFFFFUL) {
        printf("ntrdps: %s is missing. Copy it next to ntrdps.exe.\n", src);
        return 0;
    }
    if (!CopyFileA(src, dst, FALSE)) {
        err = GetLastError();
        if (GetFileAttributesA(dst) != 0xFFFFFFFFUL &&
            move_aside(dst, old, sizeof(old))) {
            if (CopyFileA(src, dst, FALSE)) {
                printf("ntrdps: copied %s\n"
                       "ntrdps: the old %s was in use (error %lu): renamed "
                       "it to %s\n", dst, dstname, (unsigned long) err, old);
                if (!MoveFileExA(old, NULL, MOVEFILE_DELAY_UNTIL_REBOOT)) {
                    printf("ntrdps: delete %s after the next boot\n", old);
                }
                return 1;
            }
            err = GetLastError();
            MoveFileA(old, dst);
        }
        if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED) {
            printf("ntrdps: %s is in use or protected. Close the program "
                   "that has it open,\nor run \"ntrdps %s\", reboot, then "
                   "install again.\n", dst, off);
        }
        mi_error("copying to the system folder", err);
        return 0;
    }
    printf("ntrdps: copied %s\n", dst);
    return 1;
}

static int make_service(void)
{
    SC_HANDLE scm, svc;
    DWORD err;
    int rc = 1;

    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (scm == NULL) {
        err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            printf("ntrdps: access denied. Only a member of the "
                   "Administrators group can\ninstall a driver.\n");
        } else {
            mi_error("OpenSCManager", err);
        }
        return 0;
    }
    svc = CreateServiceA(scm, MR_SERVICE, MR_DISPLAY, SERVICE_ALL_ACCESS,
                         SERVICE_KERNEL_DRIVER, SERVICE_SYSTEM_START,
                         SERVICE_ERROR_IGNORE, MR_IMAGE, "Video", NULL,
                         NULL, NULL, NULL);
    if (svc != NULL) {
        printf("ntrdps: kernel service \"%s\" created\n", MR_SERVICE);
    } else if (GetLastError() == ERROR_SERVICE_EXISTS) {
        svc = OpenServiceA(scm, MR_SERVICE, SERVICE_CHANGE_CONFIG);
        if (svc == NULL) {
            mi_error("OpenService " MR_SERVICE, GetLastError());
            rc = 0;
        } else if (!ChangeServiceConfigA(svc, SERVICE_KERNEL_DRIVER,
                                         SERVICE_SYSTEM_START,
                                         SERVICE_ERROR_IGNORE, MR_IMAGE,
                                         "Video", NULL, NULL, NULL, NULL,
                                         MR_DISPLAY)) {
            mi_error("ChangeServiceConfig " MR_SERVICE, GetLastError());
            rc = 0;
        } else {
            printf("ntrdps: kernel service \"%s\" switched on again\n",
                   MR_SERVICE);
        }
    } else {
        err = GetLastError();
        if (err == ERROR_SERVICE_MARKED_FOR_DELETE) {
            printf("ntrdps: the service \"%s\" is being removed. Reboot, "
                   "then try again.\n", MR_SERVICE);
        }
        mi_error("CreateService " MR_SERVICE, err);
        rc = 0;
    }
    if (svc != NULL) {
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return rc;
}

static int write_values(void)
{
    static const char drivers[] = MR_SERVICE "\0";
    HKEY k;
    DWORD disp;
    LONG e;
    unsigned i;

    e = RegCreateKeyExA(HKEY_LOCAL_MACHINE, MR_DEVKEY, 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &k,
                        &disp);
    if (e != ERROR_SUCCESS) {
        mi_error("creating HKEY_LOCAL_MACHINE\\" MR_DEVKEY, (DWORD) e);
        return 0;
    }
    e = RegSetValueExA(k, "InstalledDisplayDrivers", 0, REG_MULTI_SZ,
                       (const BYTE *) drivers, sizeof(drivers));
    for (i = 0; e == ERROR_SUCCESS && i < N_VALUES; i++) {
        e = RegSetValueExA(k, g_values[i].name, 0, REG_DWORD,
                           (const BYTE *) &g_values[i].value, sizeof(DWORD));
    }
    RegCloseKey(k);
    if (e != ERROR_SUCCESS) {
        mi_error("writing the Device0 values", (DWORD) e);
        return 0;
    }
    printf("ntrdps: wrote the display values in HKEY_LOCAL_MACHINE\\%s\n",
           MR_DEVKEY);
    return 1;
}

int mirror_install(void)
{
    DWORD v = GetVersion();
    int major = (int) (v & 0xFF), minor = (int) ((v >> 8) & 0xFF);
    int sp = sp_level();
    char sysdir[MAX_PATH], drvdir[MAX_PATH + 8];
    UINT n;

    const char *src = sp < 3 ? MR_DLL0 : MR_DLL;

    if ((v & 0x80000000UL) || major != 4 || minor != 0) {
        printf("ntrdps: the mirror driver is for Windows NT 4.0 only. "
               "This is version\n%d.%d. ntrdps reads the screen without "
               "it.\n", major, minor);
        return 1;
    }
    n = GetSystemDirectoryA(sysdir, sizeof(sysdir));
    if (n == 0 || n >= sizeof(sysdir)) {
        mi_error("GetSystemDirectory", GetLastError());
        return 1;
    }
    sprintf(drvdir, "%s\\drivers", sysdir);
    if (!copy_file(MR_SYS, drvdir, MR_SYS, "-uninstallmirror") ||
        !copy_file(src, sysdir, MR_DLL, "-uninstallmirror") ||
        !make_service() || !write_values()) {
        printf("ntrdps: the install is not complete. \"ntrdps "
               "-uninstallmirror\" switches off\nwhat was done.\n");
        return 1;
    }
    printf("ntrdps: mirror driver installed (Service Pack %d, %s).\n"
           "Reboot now: the driver loads only at boot. Until then ntrdps "
           "reads the\nscreen as before.\n"
           "If the desktop does not come back after the reboot: at boot, "
           "choose\n\"Last Known Good\" (press the space bar when NT asks), "
           "then run\n\"ntrdps -uninstallmirror\".\n", sp, src);
    return 0;
}

int mirror_uninstall(void)
{
    SC_HANDLE scm, svc;
    HKEY k;
    LONG e;
    DWORD err;
    unsigned i;
    int rc = 0;

    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm == NULL) {
        err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            printf("ntrdps: access denied. Only a member of the "
                   "Administrators group can\nchange a driver.\n");
        } else {
            mi_error("OpenSCManager", err);
        }
        return 1;
    }
    svc = OpenServiceA(scm, MR_SERVICE, SERVICE_CHANGE_CONFIG);
    if (svc == NULL) {
        err = GetLastError();
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            printf("ntrdps: the kernel service \"%s\" is not installed\n",
                   MR_SERVICE);
        } else {
            mi_error("OpenService " MR_SERVICE, err);
            rc = 1;
        }
    } else {
        if (ChangeServiceConfigA(svc, SERVICE_NO_CHANGE, SERVICE_DISABLED,
                                 SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL)) {
            printf("ntrdps: kernel service \"%s\" set to disabled\n",
                   MR_SERVICE);
        } else {
            mi_error("ChangeServiceConfig " MR_SERVICE, GetLastError());
            rc = 1;
        }
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);

    e = RegOpenKeyExA(HKEY_LOCAL_MACHINE, MR_DEVKEY, 0, KEY_SET_VALUE, &k);
    if (e == ERROR_SUCCESS) {
        e = RegDeleteValueA(k, "InstalledDisplayDrivers");
        for (i = 0; i < N_VALUES; i++) {
            if (e == ERROR_SUCCESS || e == ERROR_FILE_NOT_FOUND) {
                e = RegDeleteValueA(k, (LPSTR) g_values[i].name);
            }
        }
        RegCloseKey(k);
        if (e == ERROR_SUCCESS || e == ERROR_FILE_NOT_FOUND) {
            printf("ntrdps: removed the display values in "
                   "HKEY_LOCAL_MACHINE\\%s\n", MR_DEVKEY);
        } else {
            mi_error("removing the Device0 values", (DWORD) e);
            rc = 1;
        }
    } else if (e != ERROR_FILE_NOT_FOUND) {
        mi_error("opening HKEY_LOCAL_MACHINE\\" MR_DEVKEY, (DWORD) e);
        rc = 1;
    }
    if (rc == 0) {
        printf("ntrdps: mirror driver switched off. Reboot to unload it.\n"
               "The files %s (system32\\drivers) and %s (system32) stay;\n"
               "delete them after the reboot if you like.\n",
               MR_SYS, MR_DLL);
    }
    return rc;
}

static int layer_is_ours(const char *path)
{
    static char buf[262144];
    const size_t ml = sizeof(LY_MARK) - 1;
    HANDLE h;
    DWORD got = 0, part;
    size_t i;

    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_NOT_FOUND ? -1 : 0;
    }
    while (got < sizeof(buf) &&
           ReadFile(h, buf + got, (DWORD) sizeof(buf) - got, &part, NULL) &&
           part > 0) {
        got += part;
    }
    CloseHandle(h);
    for (i = 0; i + ml <= got; i++) {
        if (memcmp(buf + i, LY_MARK, ml) == 0) {
            return 1;
        }
    }
    return 0;
}

static int layer_switch(int on)
{
    HKEY k;
    DWORD disp, one = 1;
    LONG e;

    if (on) {
        e = RegCreateKeyExA(HKEY_LOCAL_MACHINE, LY_KEY, 0, NULL,
                            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL,
                            &k, &disp);
        if (e == ERROR_SUCCESS) {
            e = RegSetValueExA(k, LY_VALUE, 0, REG_DWORD,
                               (const BYTE *) &one, sizeof(one));
            RegCloseKey(k);
        }
        if (e != ERROR_SUCCESS) {
            mi_error("writing HKEY_LOCAL_MACHINE\\" LY_KEY "\\" LY_VALUE,
                     (DWORD) e);
            return 0;
        }
        printf("ntrdps: set HKEY_LOCAL_MACHINE\\%s\\%s = 1\n", LY_KEY,
               LY_VALUE);
        return 1;
    }
    e = RegOpenKeyExA(HKEY_LOCAL_MACHINE, LY_KEY, 0, KEY_SET_VALUE, &k);
    if (e == ERROR_SUCCESS) {
        e = RegDeleteValueA(k, LY_VALUE);
        RegCloseKey(k);
    }
    if (e != ERROR_SUCCESS && e != ERROR_FILE_NOT_FOUND) {
        mi_error("deleting HKEY_LOCAL_MACHINE\\" LY_KEY "\\" LY_VALUE,
                 (DWORD) e);
        return 0;
    }
    printf("ntrdps: deleted HKEY_LOCAL_MACHINE\\%s\\%s (it may have been "
           "missing)\n", LY_KEY, LY_VALUE);
    return 1;
}

static int layer_nt31(void)
{
    DWORD v = GetVersion();

    return !(v & 0x80000000UL) && (v & 0xFF) == 3 && ((v >> 8) & 0xFF) == 10;
}

static int same_text(const char *a, const char *b)
{
    char x, y;

    do {
        x = *a++;
        y = *b++;
        if (x >= 'A' && x <= 'Z') {
            x = (char) (x - 'A' + 'a');
        }
        if (y >= 'A' && y <= 'Z') {
            y = (char) (y - 'A' + 'a');
        }
    } while (x == y && x != '\0');
    return x == y;
}

static int layer31_free(void)
{
    HKEY k;
    char name[64];
    DWORD type = REG_NONE, cb = sizeof(name) - 1;
    LONG e;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, LY31_KEY, 0, KEY_QUERY_VALUE,
                      &k) != ERROR_SUCCESS) {
        return 1;
    }
    memset(name, 0, sizeof(name));
    e = RegQueryValueExA(k, "LayeredDriver", NULL, &type, (BYTE *) name,
                         &cb);
    RegCloseKey(k);
    if (e == ERROR_FILE_NOT_FOUND) {
        return 1;
    }
    return e == ERROR_SUCCESS && type == REG_SZ &&
           (name[0] == '\0' || same_text(name, LY31_NAME));
}

static int layer31_switch(int on)
{
    HKEY k;
    DWORD disp, v = on ? 1 : 0;
    LONG e;

    e = RegCreateKeyExA(HKEY_LOCAL_MACHINE, LY31_KEY, 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &k,
                        &disp);
    if (e == ERROR_SUCCESS) {
        if (on) {
            e = RegSetValueExA(k, "LayeredDriver", 0, REG_SZ,
                               (const BYTE *) LY31_NAME, sizeof(LY31_NAME));
        }
        if (e == ERROR_SUCCESS) {
            e = RegSetValueExA(k, "DriverEnabled", 0, REG_DWORD,
                               (const BYTE *) &v, sizeof(v));
        }
        RegCloseKey(k);
    }
    if (e != ERROR_SUCCESS) {
        mi_error("writing HKEY_LOCAL_MACHINE\\" LY31_KEY, (DWORD) e);
        return 0;
    }
    if (on) {
        printf("ntrdps: set HKEY_LOCAL_MACHINE\\%s\\LayeredDriver = %s\n",
               LY31_KEY, LY31_NAME);
    }
    printf("ntrdps: set HKEY_LOCAL_MACHINE\\%s\\DriverEnabled = %lu\n",
           LY31_KEY, (unsigned long) v);
    return 1;
}

int layer_install(void)
{
    DWORD v = GetVersion();
    int major = (int) (v & 0xFF), minor = (int) ((v >> 8) & 0xFF);
    char sysdir[MAX_PATH], dst[MAX_PATH + 16];
    UINT n;

    if ((v & 0x80000000UL) || major != 3 ||
        (minor != 10 && minor != 50 && minor != 51)) {
        printf("ntrdps: the layered driver is for Windows NT 3.1, 3.5 and "
               "3.51 only. This is\nversion %d.%d. ntrdps reads the screen "
               "without it.\n", major, minor);
        return 1;
    }
    if (layer_nt31() && !layer31_free()) {
        printf("ntrdps: HKEY_LOCAL_MACHINE\\%s\\LayeredDriver names "
               "another DLL than %s.\nAnother product may use it. Nothing "
               "was changed.\n", LY31_KEY, LY31_NAME);
        return 1;
    }
    n = GetSystemDirectoryA(sysdir, sizeof(sysdir));
    if (n == 0 || n >= sizeof(sysdir)) {
        mi_error("GetSystemDirectory", GetLastError());
        return 1;
    }
    sprintf(dst, "%s\\%s", sysdir, LY_FILE);
    if (layer_is_ours(dst) == 0) {
        printf("ntrdps: %s exists and is not ntrdps's (or cannot be "
               "read).\nAnother product may use it. Nothing was changed.\n",
               dst);
        return 1;
    }
    if (!copy_file(LY_DLL, sysdir, LY_FILE, "-uninstalllayer") ||
        !layer_switch(1) || (layer_nt31() && !layer31_switch(1))) {
        printf("ntrdps: the install is not complete. \"ntrdps "
               "-uninstalllayer\" switches off\nwhat was done.\n");
        return 1;
    }
    printf("ntrdps: layered driver installed.\n"
           "Reboot now: GDI loads it only at boot. Until then ntrdps reads "
           "the screen as\nbefore.\n"
           "If the desktop does not come back after the reboot: at boot, "
           "choose\n\"Last Known Good\" (press the space bar when NT asks), "
           "then run\n\"ntrdps -uninstalllayer\".\n");
    return 0;
}

int layer_uninstall(void)
{
    char sysdir[MAX_PATH], dll[MAX_PATH + 16], off[MAX_PATH + 16];
    UINT n;
    int rc = 0, ours;

    if (layer_nt31() && !layer31_switch(0)) {
        printf("ntrdps: nothing else was changed: without %s, NT 3.1 would "
               "find no display\ndriver while DriverEnabled is on.\n",
               LY_FILE);
        return 1;
    }
    if (!layer_switch(0)) {
        rc = 1;
    }
    n = GetSystemDirectoryA(sysdir, sizeof(sysdir));
    if (n == 0 || n >= sizeof(sysdir)) {
        mi_error("GetSystemDirectory", GetLastError());
        return 1;
    }
    sprintf(dll, "%s\\%s", sysdir, LY_FILE);
    sprintf(off, "%s\\%s", sysdir, LY_OFF);
    ours = layer_is_ours(dll);
    if (ours == 1) {
        if (MoveFileExA(dll, off, MOVEFILE_REPLACE_EXISTING)) {
            printf("ntrdps: renamed %s to %s\n", dll, LY_OFF);
        } else {
            mi_error("renaming " LY_FILE, GetLastError());
            rc = 1;
        }
    } else if (ours == 0) {
        printf("ntrdps: %s is not ntrdps's: left alone\n", dll);
    }
    if (rc == 0) {
        printf("ntrdps: layered driver switched off. Reboot to unload "
               "it.\n");
    }
    return rc;
}
