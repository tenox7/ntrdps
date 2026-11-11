/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "version.h"
#include "setup.h"

#define TITLE       "ntrdps Setup"
#define FILE_TYPE   "NTRDPSFILE"
#define SVC_NAME    "ntrdps"
#define SVC_DISPLAY "ntrdps RDP console server"
#define CONFIG_KEY  "SOFTWARE\\ntrdps"
#define REMOVED_KEY "SOFTWARE\\ntrdps-removed"
#define MR_SERVICE  "ntrdpsmr"
#define MR_SVCKEY   "SYSTEM\\CurrentControlSet\\Services\\" MR_SERVICE
#define MR_DEVKEY   MR_SVCKEY "\\Device0"
#define LY_KEY      "SYSTEM\\CurrentControlSet\\Control\\ntrdps"
#define LY_VALUE    "LayerDriver"
#define LY_FILE     "layrdisp.dll"
#define LY_OFF      "layrdisp.off"
#define LY_MARK     "ntrdpsly: ntrdps layered display driver"
#define WAIT_S      30

#ifndef SERVICE_INTERACTIVE_PROCESS
#define SERVICE_INTERACTIVE_PROCESS 0x00000100
#endif

#define R_DONE      0
#define R_NOADMIN   1
#define R_FAILED    2
#define R_NOTHING   3

#define V31         1
#define V35         2
#define V40         4

#define NOT_ADMIN   "Only an administrator can install or remove ntrdps. " \
                    "Log on as one.\n"
#define LKG_TEXT    "\nIf Windows NT does not start after the restart:\n" \
                    "press the space bar when it offers \"Last Known " \
                    "Good\", and choose it.\nThat undoes the install. " \
                    "Then run this program again and click Remove.\n"

typedef struct {
    int   id;
    char *name;
    int   vers;
} PACKED;

static PACKED g_files[] = {
#ifndef NO_NT31
    { IDF_EXE31,   "ntrdps.exe",   V31 },
    { IDF_HOOK31,  "ntrdpshk.dll", V31 },
    { IDF_LAYER31, "ntrdpsly.dll", V31 },
#endif
    { IDF_EXE35,   "ntrdps.exe",   V35 | V40 },
    { IDF_HOOK35,  "ntrdpshk.dll", V35 | V40 },
    { IDF_LAYER35, "ntrdpsly.dll", V35 },
    { IDF_MRSYS,   "ntrdpsmr.sys", V40 },
    { IDF_MRDLL,   "ntrdpsmr.dll", V40 },
    { IDF_MRDLL0,  "ntrdpsm0.dll", V40 }
};
#define N_FILES (sizeof(g_files) / sizeof(g_files[0]))

static char *g_ours[] = {
    "ntrdps.exe", "ntrdpshk.dll", "ntrdpsly.dll", "ntrdpsmr.sys",
    "ntrdpsmr.dll", "ntrdpsm0.dll", "ntrdps.log", "ntrdps.old", "ntrdps.bak",
    "hk*.dll", "nrd*.tmp"
};
#define N_OURS (sizeof(g_ours) / sizeof(g_ours[0]))

static HINSTANCE g_inst;
static int       g_ver;
static char      g_dir[MAX_PATH];
static char      g_log[16384];
static int       g_restart;
static int       g_warn;
static int       g_later;
static int       g_driver = 1;

#ifdef NTRDPS_LOGIN
#define LOGIN_VALUE "Login"
#define LUSER_VALUE "LoginUser"
#define LHASH_VALUE "LoginHash"
#define LNAME_MAX   64
#define LPW_MAX     256
#define NEED_ACCOUNT "Windows NT 3.1 and 3.5 cannot check Windows " \
                     "passwords, so ntrdps needs its own user name and " \
                     "password. Add /user:NAME /password:PASSWORD, or " \
                     "/nologin for no login.\n"

static int  g_login = 1;
static int  g_lset;
static char g_luser[2 * LNAME_MAX + 2];
static char g_lpw[2 * LPW_MAX + 2];
#endif

static void say(const char *fmt, ...)
{
    char line[1024];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(line, fmt, ap);
    va_end(ap);
    if (strlen(g_log) + strlen(line) < sizeof(g_log)) {
        strcat(g_log, line);
    }
}

static void say_text(const char *text)
{
    size_t n = strlen(g_log);

    for (; *text != '\0' && n + 1 < sizeof(g_log); text++) {
        if (*text != '\r') {
            g_log[n++] = *text;
        }
    }
    g_log[n] = '\0';
}

static int nt_version(void)
{
    DWORD v = GetVersion();
    int major = (int) (v & 0xFF), minor = (int) ((v >> 8) & 0xFF);

    if (v & 0x80000000UL) {
        return 0;
    }
#ifndef NO_NT31
    if (major == 3 && minor == 10) {
        return V31;
    }
#endif
    if (major == 3 && (minor == 50 || minor == 51)) {
        return V35;
    }
    if (major == 4 && minor == 0) {
        return V40;
    }
    return 0;
}

static int exists(char *path)
{
    return GetFileAttributesA(path) != 0xFFFFFFFFUL;
}

static int has_key(char *key)
{
    HKEY k;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &k) !=
        ERROR_SUCCESS) {
        return 0;
    }
    RegCloseKey(k);
    return 1;
}

static int has_value(char *key, char *name)
{
    HKEY k;
    BYTE buf[4];
    DWORD type, cb = sizeof(buf);
    LONG e;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key, 0, KEY_QUERY_VALUE, &k) !=
        ERROR_SUCCESS) {
        return 0;
    }
    e = RegQueryValueExA(k, name, NULL, &type, buf, &cb);
    RegCloseKey(k);
    return e == ERROR_SUCCESS || e == ERROR_MORE_DATA;
}

static int file_has(char *path, const char *mark)
{
    static char buf[262144];
    size_t ml = strlen(mark), i;
    DWORD got = 0, part;
    HANDLE h;

    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    while (got < sizeof(buf) &&
           ReadFile(h, buf + got, (DWORD) sizeof(buf) - got, &part, NULL) &&
           part > 0) {
        got += part;
    }
    CloseHandle(h);
    for (i = 0; i + ml <= got; i++) {
        if (memcmp(buf + i, mark, ml) == 0) {
            return 1;
        }
    }
    return 0;
}

static int sys_file(char *out, char *name)
{
    UINT n = GetSystemDirectoryA(out, MAX_PATH);

    if (n == 0 || n + 1 + strlen(name) >= MAX_PATH) {
        return 0;
    }
    strcat(out, "\\");
    strcat(out, name);
    return 1;
}

static void remove_file(char *path)
{
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    if (DeleteFileA(path) || !exists(path)) {
        return;
    }
    if (MoveFileExA(path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT)) {
        g_later++;
        g_restart = 1;
    } else {
        say("Cannot delete %s (error %lu).\n", path,
            (unsigned long) GetLastError());
        g_warn = 1;
    }
}

static int remove_files(char *pattern)
{
    char path[MAX_PATH + 16];
    WIN32_FIND_DATAA f;
    HANDLE find;
    int n = 0;

    sprintf(path, "%s\\%s", g_dir, pattern);
    find = FindFirstFileA(path, &f);
    if (find == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        if (!(f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            sprintf(path, "%s\\%s", g_dir, f.cFileName);
            remove_file(path);
            n++;
        }
    } while (FindNextFileA(find, &f));
    FindClose(find);
    return n;
}

static int write_file(char *path, void *data, DWORD size)
{
    HANDLE h;
    DWORD done = 0, err;
    BOOL ok;

    h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    ok = WriteFile(h, data, size, &done, NULL) && done == size &&
         FlushFileBuffers(h);
    err = GetLastError();
    CloseHandle(h);
    if (!ok) {
        DeleteFileA(path);
        SetLastError(err != 0 ? err : ERROR_WRITE_FAULT);
    }
    return ok;
}

static DWORD pe16(BYTE *p)
{
    return (DWORD) p[0] | ((DWORD) p[1] << 8);
}

static DWORD pe32(BYTE *p)
{
    return pe16(p) | (pe16(p + 2) << 16);
}

static char *self_check(void)
{
    static BYTE buf[32768];
    char path[MAX_PATH], root[4];
    BYTE *base = (BYTE *) g_inst, *nt, *sec;
    DWORD i, nsec, field, want, rva = 0, from = 0, to = 0, pos = 0, got;
    DWORD a, b, sum = 0;
    HANDLE h;
    int mem, differ = 0;

    nt = base + pe32(base + 0x3C);
    field = pe32(base + 0x3C) + 24 + 64;
    want = pe32(nt + 24 + 64);
    nsec = pe16(nt + 6);
    sec = nt + 24 + pe16(nt + 20);
    for (i = 0; i < nsec; i++, sec += 40) {
        if (memcmp(sec, ".rsrc", 6) == 0) {
            rva = pe32(sec + 12);
            from = pe32(sec + 20);
            to = from + pe32(sec + 16);
        }
    }
    if (GetModuleFileNameA(NULL, path, sizeof(path)) == 0) {
        return NULL;
    }
    mem = path[1] == ':';
    if (mem) {
        root[0] = path[0];
        strcpy(root + 1, ":\\");
        mem = GetDriveTypeA(root) == DRIVE_FIXED;
    }
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    while (ReadFile(h, buf, sizeof(buf), &got, NULL) && got > 0) {
        a = pos > from ? pos : from;
        b = pos + got < to ? pos + got : to;
        if (mem && a < b &&
            memcmp(buf + (a - pos), base + rva + (a - from), b - a) != 0) {
            differ = 1;
        }
        for (i = 0; i < got; i++) {
            if (pos + i < field || pos + i >= field + 4) {
                sum += (DWORD) buf[i] << ((pos + i) & 1 ? 8 : 0);
                sum = (sum & 0xFFFF) + (sum >> 16);
            }
        }
        pos += got;
    }
    CloseHandle(h);
    if (want != 0 && (sum & 0xFFFF) + (sum >> 16) + pos != want) {
        return "This program is damaged. Download or copy it again. "
               "With FTP, use binary mode.\n";
    }
    if (differ) {
        return "This program was copied a moment ago, and Windows NT has "
               "not written all of it to the disk yet.\nWait one minute. "
               "Then start it again.\n";
    }
    return NULL;
}

static int put_file(PACKED *f)
{
    char path[MAX_PATH + 16], old[MAX_PATH + 16];
    HRSRC r;
    HGLOBAL g = NULL;
    void *data = NULL;
    DWORD size = 0, err;

    r = FindResourceA(g_inst, MAKEINTRESOURCE(f->id), FILE_TYPE);
    if (r != NULL) {
        size = SizeofResource(g_inst, r);
        g = LoadResource(g_inst, r);
    }
    if (g != NULL) {
        data = LockResource(g);
    }
    if (data == NULL || size == 0) {
        say("This program is damaged: %s is missing in it.\n", f->name);
        return 0;
    }
    sprintf(path, "%s\\%s", g_dir, f->name);
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    if (write_file(path, data, size)) {
        return 1;
    }
    err = GetLastError();
    if (exists(path) && GetTempFileNameA(g_dir, "nrd", 0, old) != 0) {
        if (MoveFileExA(path, old, MOVEFILE_REPLACE_EXISTING)) {
            if (write_file(path, data, size)) {
                if (MoveFileExA(old, NULL, MOVEFILE_DELAY_UNTIL_REBOOT)) {
                    g_later++;
                    g_restart = 1;
                }
                return 1;
            }
            err = GetLastError();
            MoveFileExA(old, path, MOVEFILE_REPLACE_EXISTING);
        } else {
            DeleteFileA(old);
        }
    }
    say("Cannot write %s (error %lu).\n", path, (unsigned long) err);
    return 0;
}

static PACKED *packed_exe(void)
{
    unsigned i;

    for (i = 0; i < N_FILES; i++) {
        if ((g_files[i].vers & g_ver) &&
            strcmp(g_files[i].name, "ntrdps.exe") == 0) {
            return &g_files[i];
        }
    }
    return NULL;
}

static int protect(char *path, int folder)
{
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    SID_IDENTIFIER_AUTHORITY world = SECURITY_WORLD_SID_AUTHORITY;
    PSID admins = NULL, system = NULL, everyone = NULL;
    DWORD acl_buf[128];
    PACL acl = (PACL) acl_buf;
    SECURITY_DESCRIPTOR sd;
    ACE_HEADER *ace;
    DWORD i;
    int ok = 0;

    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                 &admins) &&
        AllocateAndInitializeSid(&nt, 1, SECURITY_LOCAL_SYSTEM_RID,
                                 0, 0, 0, 0, 0, 0, 0, &system) &&
        AllocateAndInitializeSid(&world, 1, SECURITY_WORLD_RID,
                                 0, 0, 0, 0, 0, 0, 0, &everyone) &&
        InitializeAcl(acl, sizeof(acl_buf), ACL_REVISION) &&
        AddAccessAllowedAce(acl, ACL_REVISION, FILE_ALL_ACCESS, admins) &&
        AddAccessAllowedAce(acl, ACL_REVISION, FILE_ALL_ACCESS, system) &&
        AddAccessAllowedAce(acl, ACL_REVISION,
                            FILE_GENERIC_READ | FILE_GENERIC_EXECUTE,
                            everyone) &&
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION)) {
        for (i = 0; folder && i < 3; i++) {
            if (GetAce(acl, i, (LPVOID *) &ace)) {
                ace->AceFlags = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
            }
        }
        ok = SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE) &&
             SetFileSecurityA(path, DACL_SECURITY_INFORMATION, &sd);
    }
    if (admins != NULL) {
        FreeSid(admins);
    }
    if (system != NULL) {
        FreeSid(system);
    }
    if (everyone != NULL) {
        FreeSid(everyone);
    }
    return ok;
}

static int protect_all(void)
{
    char root[4], path[MAX_PATH + 16];
    DWORD flags = 0, maxlen;
    WIN32_FIND_DATAA f;
    HANDLE find;
    int ok;

    root[0] = g_dir[0];
    root[1] = ':';
    root[2] = '\\';
    root[3] = '\0';
    if (!GetVolumeInformationA(root, NULL, 0, NULL, &maxlen, &flags, NULL,
                               0) || !(flags & FS_PERSISTENT_ACLS)) {
        return 1;
    }
    ok = protect(g_dir, 1);
    sprintf(path, "%s\\*.*", g_dir);
    find = FindFirstFileA(path, &f);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!(f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                sprintf(path, "%s\\%s", g_dir, f.cFileName);
                ok = protect(path, 0) && ok;
            }
        } while (FindNextFileA(find, &f));
        FindClose(find);
    }
    return ok;
}

static int svc_dir(char *dir)
{
    static DWORD buf[1024];
    QUERY_SERVICE_CONFIGA *c = (QUERY_SERVICE_CONFIGA *) buf;
    SC_HANDLE scm, svc;
    DWORD need;
    char *p, *e, root[4];
    int ok = 0;

    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm == NULL) {
        return 0;
    }
    svc = OpenServiceA(scm, SVC_NAME, SERVICE_QUERY_CONFIG);
    if (svc != NULL) {
        if (QueryServiceConfigA(svc, c, sizeof(buf), &need) &&
            c->lpBinaryPathName != NULL) {
            p = c->lpBinaryPathName;
            if (*p == '"') {
                p++;
                e = strchr(p, '"');
            } else {
                e = strchr(p, ' ');
            }
            if (e == NULL) {
                e = p + strlen(p);
            }
            if (e - p < MAX_PATH) {
                memcpy(dir, p, e - p);
                dir[e - p] = '\0';
                e = strrchr(dir, '\\');
                if (e != NULL && lstrcmpiA(e + 1, "ntrdps.exe") == 0) {
                    *e = '\0';
                    e = strrchr(dir, '\\');
                    if (e != NULL && dir[1] == ':' && e >= dir + 2 &&
                        lstrcmpiA(e + 1, "ntrdps") == 0) {
                        root[0] = dir[0];
                        root[1] = ':';
                        root[2] = '\\';
                        root[3] = '\0';
                        ok = GetDriveTypeA(root) == DRIVE_FIXED;
                    }
                }
            }
        }
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return ok;
}

static void pick_dir(void)
{
    char win[MAX_PATH];
    UINT n;

    if (svc_dir(g_dir)) {
        return;
    }
    n = GetWindowsDirectoryA(win, sizeof(win));
    if (n >= 2 && n < sizeof(win) && win[1] == ':') {
        sprintf(g_dir, "%c:\\NTRDPS", win[0]);
    } else {
        strcpy(g_dir, "C:\\NTRDPS");
    }
}

static int can_admin(void)
{
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);

    if (scm == NULL) {
        return 0;
    }
    CloseServiceHandle(scm);
    return 1;
}

static int svc_wait(SC_HANDLE svc, DWORD want)
{
    SERVICE_STATUS st;
    int i;

    for (i = 0; i < WAIT_S * 2; i++) {
        if (!QueryServiceStatus(svc, &st)) {
            return 0;
        }
        if (st.dwCurrentState == want) {
            return 1;
        }
        if (st.dwCurrentState == SERVICE_STOPPED) {
            return 0;
        }
        Sleep(500);
    }
    return 0;
}

static int svc_stop(SC_HANDLE svc)
{
    SERVICE_STATUS st;

    if (QueryServiceStatus(svc, &st) &&
        st.dwCurrentState == SERVICE_STOPPED) {
        return 1;
    }
    ControlService(svc, SERVICE_CONTROL_STOP, &st);
    return svc_wait(svc, SERVICE_STOPPED);
}

static SC_HANDLE svc_install(SC_HANDLE scm)
{
    char cmd[MAX_PATH + 32];
    DWORD type = SERVICE_WIN32_OWN_PROCESS, err;
    SC_HANDLE svc;

    if (strchr(g_dir, ' ') != NULL) {
        sprintf(cmd, "\"%s\\ntrdps.exe\" -service", g_dir);
    } else {
        sprintf(cmd, "%s\\ntrdps.exe -service", g_dir);
    }
    if (g_ver != V31) {
        type |= SERVICE_INTERACTIVE_PROCESS;
    }
    svc = OpenServiceA(scm, SVC_NAME, SERVICE_ALL_ACCESS);
    if (svc != NULL) {
        if (ChangeServiceConfigA(svc, type, SERVICE_AUTO_START,
                                 SERVICE_ERROR_NORMAL, cmd, NULL, NULL,
                                 NULL, NULL, NULL, SVC_DISPLAY)) {
            say("Service \"ntrdps\": updated. It starts at every boot.\n");
            return svc;
        }
        err = GetLastError();
        CloseServiceHandle(svc);
    } else {
        svc = CreateServiceA(scm, SVC_NAME, SVC_DISPLAY, SERVICE_ALL_ACCESS,
                             type, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                             cmd, NULL, NULL, NULL, NULL, NULL);
        if (svc != NULL) {
            say("Service \"ntrdps\": added. It starts at every boot.\n");
            return svc;
        }
        err = GetLastError();
    }
    if (err == ERROR_SERVICE_MARKED_FOR_DELETE) {
        say("The old service \"ntrdps\" is still being removed. Close the "
            "Services window of the Control Panel, or restart Windows NT. "
            "Then click Install again.\n");
    } else {
        say("Cannot add the service \"ntrdps\" (error %lu).\n",
            (unsigned long) err);
    }
    return NULL;
}

static int run_ntrdps(char *opt, char *out, DWORD cb)
{
    char exe[MAX_PATH + 16], cmd[MAX_PATH + 48], sink[256];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE rd, wr, child_wr;
    DWORD got, n = 0, code = 0;

    out[0] = '\0';
    sprintf(exe, "%s\\ntrdps.exe", g_dir);
    if (strchr(exe, ' ') != NULL) {
        sprintf(cmd, "\"%s\" %s", exe, opt);
    } else {
        sprintf(cmd, "%s %s", exe, opt);
    }
    if (!CreatePipe(&rd, &wr, NULL, 0)) {
        return -1;
    }
    if (!DuplicateHandle(GetCurrentProcess(), wr, GetCurrentProcess(),
                         &child_wr, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
        CloseHandle(rd);
        CloseHandle(wr);
        return -1;
    }
    CloseHandle(wr);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = NULL;
    si.hStdOutput = child_wr;
    si.hStdError = child_wr;
    if (!CreateProcessA(exe, cmd, NULL, NULL, TRUE, DETACHED_PROCESS, NULL,
                        g_dir, &si, &pi)) {
        sprintf(out, "%s did not run (error %lu).\n", exe,
                (unsigned long) GetLastError());
        CloseHandle(child_wr);
        CloseHandle(rd);
        return -1;
    }
    CloseHandle(child_wr);
    for (;;) {
        char *dst = n + 1 < cb ? out + n : sink;
        DWORD room = n + 1 < cb ? cb - 1 - n : sizeof(sink);

        if (!ReadFile(rd, dst, room, &got, NULL) || got == 0) {
            break;
        }
        if (dst != sink) {
            n += got;
        }
    }
    out[n] = '\0';
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, WAIT_S * 1000);
    if (!GetExitCodeProcess(pi.hProcess, &code)) {
        code = (DWORD) -1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int) code;
}

#ifdef NTRDPS_LOGIN
static int own_account(void)
{
    HMODULE adv = GetModuleHandleA("advapi32.dll");

    return adv == NULL || GetProcAddress(adv, "LogonUserW") == NULL;
}

static int get_login(void)
{
    HKEY k;
    DWORD v = 1, type, cb = sizeof(v);

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, CONFIG_KEY, 0, KEY_QUERY_VALUE,
                      &k) == ERROR_SUCCESS) {
        if (RegQueryValueExA(k, LOGIN_VALUE, NULL, &type, (LPBYTE) &v,
                             &cb) != ERROR_SUCCESS || type != REG_DWORD) {
            v = 1;
        }
        RegCloseKey(k);
    }
    return v != 0;
}

static int account_name(char *name, DWORD cb)
{
    HKEY k;
    DWORD type, n = cb;
    int ok = 0;

    name[0] = '\0';
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, CONFIG_KEY, 0, KEY_QUERY_VALUE,
                      &k) != ERROR_SUCCESS) {
        return 0;
    }
    if (RegQueryValueExA(k, LUSER_VALUE, NULL, &type, (LPBYTE) name,
                         &n) == ERROR_SUCCESS && type == REG_SZ && n > 1) {
        name[cb - 1] = '\0';
        ok = has_value(CONFIG_KEY, LHASH_VALUE);
    } else {
        name[0] = '\0';
    }
    RegCloseKey(k);
    return ok;
}

static int set_login_value(DWORD on)
{
    HKEY k;
    DWORD disp;
    LONG e;

    e = RegCreateKeyExA(HKEY_LOCAL_MACHINE, CONFIG_KEY, 0, "",
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &k,
                        &disp);
    if (e == ERROR_SUCCESS) {
        e = RegSetValueExA(k, LOGIN_VALUE, 0, REG_DWORD, (LPBYTE) &on,
                           sizeof(on));
        RegCloseKey(k);
    }
    SetLastError((DWORD) e);
    return e == ERROR_SUCCESS;
}

static int run_setlogin(char *out, DWORD cb)
{
    static char input[sizeof(g_luser) + sizeof(g_lpw) + 8];
    char exe[MAX_PATH + 16], cmd[MAX_PATH + 48], sink[256];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE rd, wr, child_wr, in_rd, in_wr, child_in;
    DWORD got, n = 0, code = 0, len, done = 0;
    BOOL ok;

    out[0] = '\0';
    sprintf(exe, "%s\\ntrdps.exe", g_dir);
    if (strchr(exe, ' ') != NULL) {
        sprintf(cmd, "\"%s\" -setlogin", exe);
    } else {
        sprintf(cmd, "%s -setlogin", exe);
    }
    sprintf(input, "%s\r\n%s\r\n", g_luser, g_lpw);
    len = (DWORD) strlen(input);
    if (!CreatePipe(&in_rd, &in_wr, NULL, 0)) {
        memset(input, 0, sizeof(input));
        return -1;
    }
    ok = WriteFile(in_wr, input, len, &done, NULL) && done == len;
    memset(input, 0, sizeof(input));
    CloseHandle(in_wr);
    if (!ok || !DuplicateHandle(GetCurrentProcess(), in_rd,
                                GetCurrentProcess(), &child_in, 0, TRUE,
                                DUPLICATE_SAME_ACCESS)) {
        CloseHandle(in_rd);
        return -1;
    }
    CloseHandle(in_rd);
    if (!CreatePipe(&rd, &wr, NULL, 0)) {
        CloseHandle(child_in);
        return -1;
    }
    if (!DuplicateHandle(GetCurrentProcess(), wr, GetCurrentProcess(),
                         &child_wr, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
        CloseHandle(rd);
        CloseHandle(wr);
        CloseHandle(child_in);
        return -1;
    }
    CloseHandle(wr);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = child_in;
    si.hStdOutput = child_wr;
    si.hStdError = child_wr;
    if (!CreateProcessA(exe, cmd, NULL, NULL, TRUE, DETACHED_PROCESS, NULL,
                        g_dir, &si, &pi)) {
        sprintf(out, "%s did not run (error %lu).\n", exe,
                (unsigned long) GetLastError());
        CloseHandle(child_wr);
        CloseHandle(child_in);
        CloseHandle(rd);
        return -1;
    }
    CloseHandle(child_wr);
    CloseHandle(child_in);
    for (;;) {
        char *dst = n + 1 < cb ? out + n : sink;
        DWORD room = n + 1 < cb ? cb - 1 - n : sizeof(sink);

        if (!ReadFile(rd, dst, room, &got, NULL) || got == 0) {
            break;
        }
        if (dst != sink) {
            n += got;
        }
    }
    out[n] = '\0';
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, WAIT_S * 1000);
    if (!GetExitCodeProcess(pi.hProcess, &code)) {
        code = (DWORD) -1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int) code;
}

static void login_setup(void)
{
    char out[1024], name[2 * LNAME_MAX + 2];
    int rc;

    if (!set_login_value((DWORD) g_login)) {
        say("Cannot write the setting Login (error %lu).\n",
            (unsigned long) GetLastError());
        g_warn = 1;
    }
    if (!g_login) {
        say("Login: off. Anyone who can reach TCP port 3389 gets the "
            "console.\n");
    } else if (!own_account()) {
        say("Login: on. RDP clients log on with an administrator account "
            "of this computer.\n");
        if (g_lset) {
            say("(/user: and /password: are not used: Windows NT 3.51 and "
                "4.0 check Windows accounts.)\n");
        }
    } else if (g_lset) {
        rc = run_setlogin(out, sizeof(out));
        if (rc == 0) {
            say("Login: on, with the ntrdps account \"%s\".\n", g_luser);
        } else {
            say("Login: the ntrdps account was not set. What ntrdps "
                "said:\n");
            say_text(out);
            g_warn = 1;
        }
    } else {
        account_name(name, sizeof(name));
        say("Login: on, with the ntrdps account \"%s\" (unchanged).\n",
            name);
    }
    memset(g_lpw, 0, sizeof(g_lpw));
    g_lset = 0;
}
#endif

static int driver_on(void)
{
    if (g_ver == V40) {
        return has_value(MR_DEVKEY, "InstalledDisplayDrivers");
    }
    return has_value(LY_KEY, LY_VALUE);
}

static int driver_there(void)
{
    char path[MAX_PATH + 16];

    if (g_ver == V40) {
        return has_key(MR_SVCKEY) ||
               (sys_file(path, "drivers\\ntrdpsmr.sys") && exists(path)) ||
               (sys_file(path, "ntrdpsmr.dll") && exists(path));
    }
    return has_value(LY_KEY, LY_VALUE) ||
           (sys_file(path, LY_FILE) && file_has(path, LY_MARK)) ||
           (sys_file(path, LY_OFF) && file_has(path, LY_MARK));
}

static int svc_there(void)
{
    SC_HANDLE scm, svc;
    int there = 0;

    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm != NULL) {
        svc = OpenServiceA(scm, SVC_NAME, SERVICE_QUERY_STATUS);
        if (svc != NULL) {
            there = 1;
            CloseServiceHandle(svc);
        }
        CloseServiceHandle(scm);
    }
    return there;
}

static int installed(void)
{
    char exe[MAX_PATH + 16];

    sprintf(exe, "%s\\ntrdps.exe", g_dir);
    return svc_there() || exists(exe) || has_key(CONFIG_KEY) ||
           driver_there();
}

static int do_install(void)
{
    SC_HANDLE scm, svc;
    char out[4096];
    unsigned i, n = 0;
    DWORD err;
    int rc;

    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (scm == NULL) {
        say(NOT_ADMIN);
        return R_NOADMIN;
    }
#ifdef NTRDPS_LOGIN
    if (g_login && own_account() && !g_lset && !account_name(out, 256)) {
        say(NEED_ACCOUNT);
        CloseServiceHandle(scm);
        return R_FAILED;
    }
#endif
    svc = OpenServiceA(scm, SVC_NAME, SERVICE_ALL_ACCESS);
    if (svc != NULL) {
        if (!svc_stop(svc)) {
            say("The service \"ntrdps\" did not stop within %d seconds.\n",
                WAIT_S);
        }
        CloseServiceHandle(svc);
    }
    if (!CreateDirectoryA(g_dir, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        say("Cannot make the folder %s (error %lu).\n", g_dir,
            (unsigned long) GetLastError());
        CloseServiceHandle(scm);
        return R_FAILED;
    }
    for (i = 0; i < N_FILES; i++) {
        if (g_files[i].vers & g_ver) {
            if (!put_file(&g_files[i])) {
                CloseServiceHandle(scm);
                return R_FAILED;
            }
            n++;
        }
    }
    say("Files: %u copied to %s.\n", n, g_dir);
    if (!protect_all()) {
        say("Warning: cannot set the permissions of %s. Let only "
            "Administrators and SYSTEM change it.\n", g_dir);
        g_warn = 1;
    }
    svc = svc_install(scm);
    if (svc == NULL) {
        CloseServiceHandle(scm);
        return R_FAILED;
    }
    if (g_driver) {
        rc = run_ntrdps(g_ver == V40 ? "-installmirror" : "-installlayer", out,
                        sizeof(out));
        if (rc == 0) {
            say("Display driver: installed. It loads at the next restart.\n");
            g_restart = 1;
        } else {
            say("Display driver: not installed. ntrdps works without it, only "
                "slower. What ntrdps said:\n");
            say_text(out);
            g_warn = 1;
        }
    } else if (driver_on() || (g_ver != V40 && driver_there())) {
        rc = run_ntrdps(g_ver == V40 ? "-uninstallmirror" : "-uninstalllayer",
                        out, sizeof(out));
        if (rc == 0) {
            say("Display driver: switched off (you chose reading the screen). "
                "It goes off at the next restart.\n");
            g_restart = 1;
        } else {
            say("Display driver: could not switch it off. What ntrdps said:\n");
            say_text(out);
            g_warn = 1;
        }
    } else {
        say("Display driver: not installed (you chose reading the screen). "
            "ntrdps reads the screen. The driver files are here too, so you "
            "can switch it on later: see the README.\n");
    }
#ifdef NTRDPS_LOGIN
    login_setup();
#endif
    if (!StartServiceA(svc, 0, NULL) &&
        GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
        err = GetLastError();
        say("Service \"ntrdps\": did not start (error %lu).\n",
            (unsigned long) err);
        g_warn = 1;
    } else if (svc_wait(svc, SERVICE_RUNNING)) {
        say("Service \"ntrdps\": running. RDP clients can connect now.\n");
    } else {
        say("Service \"ntrdps\": did not start. Its log: %s\\ntrdps.log\n",
            g_dir);
        g_warn = 1;
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return R_DONE;
}

static void mark_removed(void)
{
    HKEY k;
    DWORD disp;

    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, REMOVED_KEY, 0, "",
                        REG_OPTION_VOLATILE, KEY_WRITE, NULL, &k,
                        &disp) == ERROR_SUCCESS) {
        RegCloseKey(k);
    }
}

static int do_remove(void)
{
    SC_HANDLE scm, svc;
    char out[4096], path[MAX_PATH + 16];
    PACKED *exe;
    unsigned i;
    int found = 0, off = 1, rc, n, later;

    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (scm == NULL) {
        say(NOT_ADMIN);
        return R_NOADMIN;
    }

    svc = OpenServiceA(scm, SVC_NAME, SERVICE_ALL_ACCESS);
    if (svc != NULL) {
        found = 1;
        if (!svc_stop(svc)) {
            say("The service \"ntrdps\" did not stop within %d seconds.\n",
                WAIT_S);
            g_restart = 1;
        }
        if (DeleteService(svc) ||
            GetLastError() == ERROR_SERVICE_MARKED_FOR_DELETE) {
            say("Service \"ntrdps\": removed.\n");
        } else {
            say("Cannot remove the service \"ntrdps\" (error %lu).\n",
                (unsigned long) GetLastError());
            g_warn = 1;
        }
        CloseServiceHandle(svc);
    }
    if (RegDeleteKeyA(HKEY_LOCAL_MACHINE, CONFIG_KEY) == ERROR_SUCCESS) {
        found = 1;
        say("Settings: removed.\n");
    }

    if (driver_on() || (g_ver != V40 && driver_there())) {
        found = 1;
        exe = packed_exe();
        CreateDirectoryA(g_dir, NULL);
        if (exe == NULL || !put_file(exe)) {
            rc = -1;
            strcpy(out, "");
        } else {
            rc = run_ntrdps(g_ver == V40 ? "-uninstallmirror" :
                            "-uninstalllayer", out, sizeof(out));
        }
        if (rc == 0) {
            say("Display driver: switched off.\n");
            g_restart = 1;
        } else {
            say("Display driver: cannot switch it off. What ntrdps said:\n");
            say_text(out);
            g_warn = 1;
            off = 0;
        }
    }
    if (off && g_ver == V40) {
        svc = OpenServiceA(scm, MR_SERVICE, DELETE);
        if (svc != NULL) {
            found = 1;
            DeleteService(svc);
            CloseServiceHandle(svc);
            g_restart = 1;
        }
        if (sys_file(path, "drivers\\ntrdpsmr.sys") && exists(path)) {
            found = 1;
            remove_file(path);
        }
        if (sys_file(path, "ntrdpsmr.dll") && exists(path)) {
            found = 1;
            remove_file(path);
        }
    } else if (off) {
        if (sys_file(path, LY_OFF) && file_has(path, LY_MARK)) {
            found = 1;
            remove_file(path);
        }
        RegDeleteKeyA(HKEY_LOCAL_MACHINE, LY_KEY);
    }
    CloseServiceHandle(scm);

    if (exists(g_dir)) {
        later = g_later;
        n = 0;
        for (i = 0; i < N_OURS; i++) {
            n += remove_files(g_ours[i]);
        }
        later = g_later - later;
        if (n > 0 && later > 0) {
            found = 1;
            say("Files: %d deleted from %s, %d more at the next restart.\n",
                n - later, g_dir, later);
        } else if (n > 0) {
            found = 1;
            say("Files: %d deleted from %s.\n", n, g_dir);
        }
        if (RemoveDirectoryA(g_dir)) {
            found = 1;
            say("Folder %s: deleted.\n", g_dir);
        } else if (later > 0 &&
                   MoveFileExA(g_dir, NULL, MOVEFILE_DELAY_UNTIL_REBOOT)) {
            say("Folder %s: goes away at the next restart.\n", g_dir);
            g_restart = 1;
        } else if (n > 0) {
            say("Folder %s: has other files, so it stays.\n", g_dir);
        }
    }
    if (!found) {
        say("ntrdps is not installed. There is nothing to remove.\n");
        return R_NOTHING;
    }
    if (g_restart && !g_warn) {
        mark_removed();
    }
    return R_DONE;
}

#ifndef NTRDPS_LOGIN
static void plan_text(char *t)
{
    if (g_ver == 0) {
        strcpy(t, "Nothing. ntrdps runs only on Windows NT 3.1, 3.5, 3.51 "
                  "and 4.0.");
        return;
    }
    sprintf(t,
            "1. Copy %s to %s.\n"
            "2. Add the service \"ntrdps\". It starts at every boot.\n"
            "%s\n"
            "4. Start the service. RDP clients can connect then.\n"
            "\n"
            "Remove takes all of it away again, the settings too.",
            g_ver == V40 ? "ntrdps.exe, ntrdpshk.dll, ntrdpsmr.sys, "
                           "ntrdpsmr.dll and ntrdpsm0.dll" :
                           "ntrdps.exe, ntrdpshk.dll and ntrdpsly.dll",
            g_dir,
            g_driver ?
                (g_ver == V40 ?
                 "3. Add the display driver ntrdpsmr to System32. It makes the "
                 "screen faster. It loads at the next restart." :
                 "3. Add the display driver: ntrdpsly.dll goes to System32 as "
                 "layrdisp.dll. It makes the screen faster. It loads at the "
                 "next restart.") :
                "3. No display driver (you chose reading the screen - no "
                "reboot). The driver files are copied too, so you can switch "
                "it on later.");
}
#endif

static void status_text(char *t, int admin)
{
    SC_HANDLE scm, svc;
    SERVICE_STATUS st;
    char *state = "missing";

    if (g_ver == 0) {
        strcpy(t, "This is not Windows NT 3.1, 3.5, 3.51 or 4.0.");
        return;
    }
    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm != NULL) {
        svc = OpenServiceA(scm, SVC_NAME, SERVICE_QUERY_STATUS);
        if (svc != NULL) {
            state = "stopped";
            if (QueryServiceStatus(svc, &st) &&
                st.dwCurrentState != SERVICE_STOPPED) {
                state = st.dwCurrentState == SERVICE_RUNNING ? "running" :
                        "starting or stopping";
            }
            CloseServiceHandle(svc);
        }
        CloseServiceHandle(scm);
    }
    if (has_key(REMOVED_KEY)) {
        strcpy(t, "Removed. Restart Windows NT to finish.");
    } else if (!installed()) {
        strcpy(t, "Not installed.");
    } else {
#ifdef NTRDPS_LOGIN
        sprintf(t, "Installed in %s.\nService: %s. Display driver: %s. "
                "Login: %s.", g_dir, state, driver_on() ? "on" : "off",
                get_login() ? "on" : "off");
#else
        sprintf(t, "Installed in %s.\nService: %s. Display driver: %s.",
                g_dir, state, driver_on() ? "on" : "off");
#endif
    }
    if (!admin) {
        strcat(t, "\nLog on as an administrator to install or remove.");
    }
}

static void restart(HWND owner)
{
    HANDLE tok;
    TOKEN_PRIVILEGES tp;

    if (OpenProcessToken(GetCurrentProcess(),
                         TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValueA(NULL, SE_SHUTDOWN_NAME,
                                  &tp.Privileges[0].Luid)) {
            AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL);
        }
        CloseHandle(tok);
    }
    if (!ExitWindowsEx(EWX_REBOOT, 0)) {
        MessageBoxA(owner, "Windows NT did not restart. Please restart it "
                    "yourself.", TITLE, MB_OK | MB_ICONEXCLAMATION);
    }
}

static void report(HWND owner, int r, int install)
{
    static char text[sizeof(g_log) + 512];
    UINT icon = (r == R_DONE && !g_warn) || r == R_NOTHING ?
                MB_ICONINFORMATION : MB_ICONEXCLAMATION;

    strcpy(text, g_log);
    if (r == R_DONE && g_restart) {
        if (install && g_driver) {
            strcat(text, LKG_TEXT);
        }
        strcat(text, "\nRestart Windows NT now?");
        if (MessageBoxA(owner, text, TITLE, MB_YESNO | icon) == IDYES) {
            restart(owner);
        }
    } else {
        MessageBoxA(owner, text, TITLE, MB_OK | icon);
    }
}

static void refresh(HWND w)
{
    char text[2048];
    int admin = can_admin();
    int can = g_ver != 0 && admin && !has_key(REMOVED_KEY);

#ifndef NTRDPS_LOGIN
    plan_text(text);
    SetDlgItemTextA(w, IDC_PLAN, text);
#endif
    status_text(text, admin);
    SetDlgItemTextA(w, IDC_STATUS, text);
    EnableWindow(GetDlgItem(w, IDC_INSTALL), can);
    EnableWindow(GetDlgItem(w, IDC_REMOVE), can && installed());
    SetFocus(GetDlgItem(w, can ? IDC_INSTALL : IDCANCEL));
}

static void center(HWND w)
{
    RECT r;
    int x, y;

    GetWindowRect(w, &r);
    x = (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2;
    y = (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 2;
    SetWindowPos(w, NULL, x > 0 ? x : 0, y > 0 ? y : 0, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER);
}

#ifdef NTRDPS_LOGIN
static BOOL CALLBACK account_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    static char again[sizeof(g_lpw)];
    char text[512], name[2 * LNAME_MAX + 2];

    (void) lp;
    switch (msg) {
    case WM_INITDIALOG:
        if (account_name(name, sizeof(name))) {
            sprintf(text, "Windows NT 3.1 and 3.5 cannot check Windows "
                    "passwords, so ntrdps has its own account: \"%.64s\".\n"
                    "Leave all fields empty to keep it, or type a new user "
                    "name and password.", name);
        } else {
            strcpy(text, "Windows NT 3.1 and 3.5 cannot check Windows "
                   "passwords. Choose a user name and a password for "
                   "ntrdps: RDP clients log on with them.");
        }
        SetDlgItemTextA(w, IDC_ACC_TEXT, text);
        SendDlgItemMessageA(w, IDC_LUSER, EM_LIMITTEXT, LNAME_MAX, 0);
        SendDlgItemMessageA(w, IDC_LPW, EM_LIMITTEXT, LPW_MAX, 0);
        SendDlgItemMessageA(w, IDC_LPW2, EM_LIMITTEXT, LPW_MAX, 0);
        center(w);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK:
            GetDlgItemTextA(w, IDC_LUSER, g_luser, sizeof(g_luser));
            GetDlgItemTextA(w, IDC_LPW, g_lpw, sizeof(g_lpw));
            GetDlgItemTextA(w, IDC_LPW2, again, sizeof(again));
            text[0] = '\0';
            g_lset = 0;
            if (g_luser[0] == '\0' && g_lpw[0] == '\0' &&
                again[0] == '\0' && account_name(name, sizeof(name))) {
            } else if (g_luser[0] == '\0' || strchr(g_luser, '\\') != NULL) {
                strcpy(text, "Type a user name (without \\).");
            } else if (g_lpw[0] == '\0') {
                strcpy(text, "Type a password.");
            } else if (strcmp(g_lpw, again) != 0) {
                strcpy(text, "The two passwords differ. Type them again.");
                SetDlgItemTextA(w, IDC_LPW, "");
                SetDlgItemTextA(w, IDC_LPW2, "");
            } else {
                g_lset = 1;
            }
            memset(again, 0, sizeof(again));
            if (text[0] != '\0') {
                memset(g_lpw, 0, sizeof(g_lpw));
                MessageBoxA(w, text, TITLE, MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
            EndDialog(w, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(w, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static int ask_login(HWND w)
{
    g_login = IsDlgButtonChecked(w, IDC_CB_LOGIN) ? 1 : 0;
    g_lset = 0;
    if (!g_login || g_ver == 0 || !own_account()) {
        return 1;
    }
    return DialogBoxA(g_inst, MAKEINTRESOURCE(IDD_ACCOUNT), w,
                      (DLGPROC) account_proc) == IDOK;
}
#endif

static void act(HWND w, int id)
{
    HCURSOR old;
    int r;

#ifdef NTRDPS_LOGIN
    if (id == IDC_INSTALL && !ask_login(w)) {
        return;
    }
#endif
    EnableWindow(GetDlgItem(w, IDC_INSTALL), FALSE);
    EnableWindow(GetDlgItem(w, IDC_REMOVE), FALSE);
    old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    if (id == IDC_INSTALL) {
        g_driver = IsDlgButtonChecked(w, IDC_RB_DRIVER) ? 1 : 0;
    }
    g_log[0] = '\0';
    g_restart = g_warn = g_later = 0;
    r = id == IDC_INSTALL ? do_install() : do_remove();
    SetCursor(old);
    refresh(w);
    report(w, r, id == IDC_INSTALL);
}

static BOOL CALLBACK dlg_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
#ifndef NTRDPS_LOGIN
    char buf[2048];
#endif

    (void) lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowTextA(w, "ntrdps " NTRDPS_VERSION " Setup");
        g_driver = 1;
        CheckRadioButton(w, IDC_RB_DRIVER, IDC_RB_POLL, IDC_RB_DRIVER);
#ifdef NTRDPS_LOGIN
        g_login = get_login();
        CheckDlgButton(w, IDC_CB_LOGIN, g_login ? 1 : 0);
        if (g_ver != V40) {
            SetDlgItemTextA(w, IDC_RB_DRIVER,
                            "&Layered Driver (fast, runs in CSRSS)");
        }
#endif
        center(w);
        refresh(w);
        return FALSE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_INSTALL:
        case IDC_REMOVE:
            act(w, LOWORD(wp));
            return TRUE;
#ifdef NTRDPS_LOGIN
        case IDC_CB_LOGIN:
            g_login = IsDlgButtonChecked(w, IDC_CB_LOGIN) ? 1 : 0;
            return TRUE;
#endif
        case IDC_RB_DRIVER:
        case IDC_RB_POLL:
            g_driver = LOWORD(wp) == IDC_RB_DRIVER;
#ifndef NTRDPS_LOGIN
            plan_text(buf);
            SetDlgItemTextA(w, IDC_PLAN, buf);
#endif
            return TRUE;
        case IDCANCEL:
            EndDialog(w, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static int has_opt(char *cmd, char *word)
{
    size_t n = strlen(word);
    char *p = cmd;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if ((*p == '/' || *p == '-') && _strnicmp(p + 1, word, n) == 0 &&
            (p[n + 1] == '\0' || p[n + 1] == ' ' || p[n + 1] == '\t')) {
            return 1;
        }
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
    }
    return 0;
}

#ifdef NTRDPS_LOGIN
static int opt_value(char *cmd, char *name, char *out, size_t cap)
{
    size_t n = strlen(name), k = 0;
    char *p = cmd;
    int quoted;

    out[0] = '\0';
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if ((*p == '/' || *p == '-') && _strnicmp(p + 1, name, n) == 0) {
            p += n + 1;
            quoted = *p == '"';
            if (quoted) {
                p++;
            }
            while (*p != '\0' && (quoted ? *p != '"' :
                                  (*p != ' ' && *p != '\t'))) {
                if (k + 1 < cap) {
                    out[k++] = *p;
                }
                p++;
            }
            out[k] = '\0';
            return 1;
        }
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
    }
    return 0;
}
#endif

static void to_stdout(char *text)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD done;

    if (h == NULL || h == INVALID_HANDLE_VALUE) {
        return;
    }
    for (; *text != '\0'; text++) {
        if (*text == '\n') {
            WriteFile(h, "\r\n", 2, &done, NULL);
        } else {
            WriteFile(h, text, 1, &done, NULL);
        }
    }
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    int quiet, install, r;
    char *bad;

    (void) prev;
    (void) show;
    g_inst = inst;
    g_ver = nt_version();
    pick_dir();
    quiet = has_opt(cmd, "q");
    install = has_opt(cmd, "install");
    g_driver = !has_opt(cmd, "poll") && !has_opt(cmd, "nodriver");
#ifdef NTRDPS_LOGIN
    g_login = !has_opt(cmd, "nologin");
    r = opt_value(cmd, "user:", g_luser, sizeof(g_luser));
    r += opt_value(cmd, "password:", g_lpw, sizeof(g_lpw));
    if (install && r > 0) {
        if (r != 2 || g_luser[0] == '\0' || g_lpw[0] == '\0' ||
            strchr(g_luser, '\\') != NULL) {
            memset(g_lpw, 0, sizeof(g_lpw));
            say("Give both /user:NAME and /password:PASSWORD (a user name "
                "without \\).\n");
            if (quiet) {
                to_stdout(g_log);
            } else {
                MessageBoxA(NULL, g_log, TITLE, MB_OK | MB_ICONEXCLAMATION);
            }
            return R_FAILED;
        }
        g_lset = 1;
    }
#endif
    if (has_opt(cmd, "status")) {
        status_text(g_log, can_admin());
        strcat(g_log, "\n");
        if (quiet) {
            to_stdout(g_log);
        } else {
            MessageBoxA(NULL, g_log, TITLE, MB_OK | MB_ICONINFORMATION);
        }
        return R_DONE;
    }
    bad = self_check();
    if (bad != NULL) {
        say("%s", bad);
        if (quiet) {
            to_stdout(g_log);
        } else {
            MessageBoxA(NULL, g_log, TITLE, MB_OK | MB_ICONEXCLAMATION);
        }
        return R_FAILED;
    }
    if (install || has_opt(cmd, "remove")) {
        if (g_ver == 0) {
            say("ntrdps runs only on Windows NT 3.1, 3.5, 3.51 and 4.0.\n");
            r = R_FAILED;
        } else if (has_key(REMOVED_KEY)) {
            say("ntrdps was removed. Restart Windows NT to finish. Then "
                "you can install it again.\n");
            r = install ? R_FAILED : R_NOTHING;
        } else {
            r = install ? do_install() : do_remove();
        }
        if (quiet) {
            if (r == R_DONE && g_restart) {
                say("Restart Windows NT to finish.\n");
            }
            to_stdout(g_log);
        } else {
            report(NULL, r, install);
        }
        return r;
    }
    DialogBoxA(inst, MAKEINTRESOURCE(IDD_SETUP), NULL, (DLGPROC) dlg_proc);
    return R_DONE;
}
