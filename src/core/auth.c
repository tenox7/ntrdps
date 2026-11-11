/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <string.h>
#include <stdio.h>
#include "log.h"
#include "config.h"
#include "pwhash.h"
#include "auth.h"

#ifndef LOGON32_LOGON_NETWORK
#define LOGON32_LOGON_NETWORK     3
#endif
#ifndef LOGON32_PROVIDER_DEFAULT
#define LOGON32_PROVIDER_DEFAULT  0
#endif
#ifndef DOMAIN_USER_RID_GUEST
#define DOMAIN_USER_RID_GUEST     0x1F5
#endif

typedef BOOL (WINAPI *LOGON_USER_W)(LPWSTR, LPWSTR, LPWSTR, DWORD, DWORD,
                                    PHANDLE);

static LOGON_USER_W g_logon;
static int          g_no_lsa;
static CRITICAL_SECTION g_one;
static int          g_one_ready;
static WCHAR        g_user[AUTH_NAME_MAX + 1];
static char         g_hash[PWHASH_TEXT];

static unsigned int wlen(const WCHAR *s)
{
    unsigned int n = 0;

    while (s[n] != 0) {
        n++;
    }
    return n;
}

void auth_log_name(char *out, unsigned int cap, const WCHAR *name)
{
    unsigned int n = 0;

    while (*name != 0 && n + 1 < cap) {
        out[n++] = (*name >= 0x20 && *name < 0x7F) ? (char) *name : '?';
        name++;
    }
    out[n] = '\0';
}

static void read_account(void)
{
    HKEY key;
    DWORD type = REG_NONE, len;

    g_user[0] = 0;
    g_hash[0] = '\0';
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, CONFIG_KEY, 0, KEY_QUERY_VALUE,
                      &key) != ERROR_SUCCESS) {
        return;
    }
    len = sizeof(g_user) - sizeof(WCHAR);
    if (RegQueryValueExW(key, (LPWSTR) L"LoginUser", NULL, &type,
                         (LPBYTE) g_user, &len) != ERROR_SUCCESS ||
        type != REG_SZ) {
        g_user[0] = 0;
    } else {
        g_user[len / sizeof(WCHAR)] = 0;
    }
    len = sizeof(g_hash) - 1;
    if (RegQueryValueExA(key, AUTH_VALUE_HASH, NULL, &type,
                         (LPBYTE) g_hash, &len) != ERROR_SUCCESS ||
        type != REG_SZ) {
        g_hash[0] = '\0';
    } else {
        g_hash[len] = '\0';
    }
    RegCloseKey(key);
}

void auth_start(void)
{
    HMODULE adv = GetModuleHandleA("advapi32.dll");
    char name[AUTH_NAME_MAX + 1];

    if (!g_one_ready) {
        InitializeCriticalSection(&g_one);
        g_one_ready = 1;
    }
    g_logon = adv != NULL
        ? (LOGON_USER_W) GetProcAddress(adv, "LogonUserW") : NULL;
    if (g_logon != NULL) {
        log_msg(LOG_INFO, "login: Windows accounts (LogonUser); only "
                "members of Administrators get the console");
        return;
    }
    read_account();
    if (g_user[0] == 0 || g_hash[0] == '\0') {
        log_msg(LOG_WARN, "login: this NT has no LogonUser, and no ntrdps "
                "account is set (settings " AUTH_VALUE_USER " and "
                AUTH_VALUE_HASH "): nobody can log in. Set one with the "
                "installer or \"ntrdps -setlogin\"");
        return;
    }
    auth_log_name(name, sizeof(name), g_user);
    log_msg(LOG_INFO, "login: the ntrdps account \"%s\" (this NT has no "
            "LogonUser)", name);
}

int auth_windows(void)
{
    return g_logon != NULL;
}

int auth_usable(void)
{
    return g_logon != NULL || (g_user[0] != 0 && g_hash[0] != '\0');
}

static int is_admin(HANDLE tok)
{
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    TOKEN_GROUPS *g;
    PSID admins = NULL;
    DWORD need = 0, i;
    int yes = 0;

    GetTokenInformation(tok, TokenGroups, NULL, 0, &need);
    if (need == 0) {
        return 0;
    }
    g = (TOKEN_GROUPS *) LocalAlloc(LMEM_FIXED, need);
    if (g == NULL) {
        return 0;
    }
    if (GetTokenInformation(tok, TokenGroups, g, need, &need) &&
        AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                 &admins)) {
        for (i = 0; i < g->GroupCount; i++) {
            if (EqualSid(g->Groups[i].Sid, admins)) {
                yes = 1;
                break;
            }
        }
        FreeSid(admins);
    }
    LocalFree((HLOCAL) g);
    return yes;
}

static int is_guest(HANDLE tok)
{
    TOKEN_USER *u;
    DWORD need = 0;
    PUCHAR n;
    int yes = 0;

    GetTokenInformation(tok, TokenUser, NULL, 0, &need);
    if (need == 0) {
        return 0;
    }
    u = (TOKEN_USER *) LocalAlloc(LMEM_FIXED, need);
    if (u == NULL) {
        return 0;
    }
    if (GetTokenInformation(tok, TokenUser, u, need, &need) &&
        IsValidSid(u->User.Sid)) {
        n = GetSidSubAuthorityCount(u->User.Sid);
        yes = *n > 0 && *GetSidSubAuthority(u->User.Sid, *n - 1) ==
                        DOMAIN_USER_RID_GUEST;
    }
    LocalFree((HLOCAL) u);
    return yes;
}

static const char *logon_error(DWORD e)
{
    switch (e) {
    case 1326: return "wrong user name or password";
    case 1327: return "account restriction";
    case 1328: return "not allowed at this time";
    case 1329: return "not allowed from this computer";
    case 1330: return "password expired";
    case 1331: return "account disabled";
    case 1385: return "no right to access this computer from the network";
    case 1907: return "the password must change";
    case 1909: return "account locked out";
    case 1314: return "ntrdps lacks the privilege: run it as the service";
    }
    return "";
}

static int windows_check(const WCHAR *user, const WCHAR *domain,
                         const WCHAR *pw, const char *peer)
{
    char lu[AUTH_NAME_MAX + 1], ld[AUTH_NAME_MAX + 1];
    HANDLE tok = NULL;
    DWORD e;
    int admin, guest;

    auth_log_name(lu, sizeof(lu), user);
    auth_log_name(ld, sizeof(ld), domain);
    if (g_no_lsa) {
        log_msg(LOG_WARN, "login %s: user \"%s\": refused (LogonUser "
                "said error 1314 before: ntrdps must run as LocalSystem)",
                peer, lu);
        return AUTH_ERROR;
    }
    if (!g_logon((LPWSTR) user, (LPWSTR) domain, (LPWSTR) pw,
                 LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT, &tok)) {
        e = GetLastError();
        log_msg(LOG_WARN, "login %s: user \"%s\", domain \"%s\": refused "
                "(LogonUser error %lu%s%s)", peer, lu, ld, (unsigned long) e,
                logon_error(e)[0] ? ": " : "", logon_error(e));
        if (e == 1314) {
            g_no_lsa = 1;
            return AUTH_ERROR;
        }
        return AUTH_BAD;
    }
    admin = is_admin(tok);
    guest = !admin && is_guest(tok);
    CloseHandle(tok);
    if (guest) {
        log_msg(LOG_WARN, "login %s: user \"%s\", domain \"%s\": Windows "
                "let it in as the Guest account (a user name it does not "
                "know gets Guest while Guest is on), not an administrator: "
                "refused", peer, lu, ld);
        return AUTH_NOT_ADMIN;
    }
    if (!admin) {
        log_msg(LOG_WARN, "login %s: user \"%s\", domain \"%s\": the "
                "password is right, but the account is not in "
                "Administrators: refused", peer, lu, ld);
        return AUTH_NOT_ADMIN;
    }
    log_msg(LOG_INFO, "login %s: user \"%s\", domain \"%s\": an "
            "administrator: allowed", peer, lu, ld);
    return AUTH_OK;
}

static int own_check(const WCHAR *user, const WCHAR *pw, const char *peer)
{
    char lu[AUTH_NAME_MAX + 1];
    int ok;

    auth_log_name(lu, sizeof(lu), user);
    if (g_user[0] == 0 || g_hash[0] == '\0') {
        log_msg(LOG_WARN, "login %s: user \"%s\": refused, no ntrdps "
                "account is set", peer, lu);
        return AUTH_NO_USER;
    }
    ok = pwhash_check(pw, wlen(pw), g_hash);
    if (ok < 0) {
        log_msg(LOG_ERROR, "login %s: the setting " AUTH_VALUE_HASH " is "
                "not readable: set the account again", peer);
        return AUTH_NO_USER;
    }
    if (ok != 1 || lstrcmpiW((LPWSTR) user, g_user) != 0) {
        log_msg(LOG_WARN, "login %s: user \"%s\": refused (wrong user name "
                "or password)", peer, lu);
        return AUTH_BAD;
    }
    log_msg(LOG_INFO, "login %s: user \"%s\": the ntrdps account: allowed",
            peer, lu);
    return AUTH_OK;
}

int auth_check(const WCHAR *user, const WCHAR *domain, const WCHAR *pw,
               const char *peer)
{
    WCHAR u[AUTH_NAME_MAX + 1], d[AUTH_NAME_MAX + 1];
    unsigned int i, k = 0, bs = 0;
    int r;

    for (i = 0; user[i] != 0; i++) {
        if (user[i] == '\\') {
            bs = i + 1;
        }
    }
    for (i = bs; user[i] != 0 && k < AUTH_NAME_MAX; i++) {
        u[k++] = user[i];
    }
    u[k] = 0;
    k = 0;
    if (bs > 0) {
        for (i = 0; i + 1 < bs && k < AUTH_NAME_MAX; i++) {
            d[k++] = user[i];
        }
    } else {
        for (i = 0; domain[i] != 0 && k < AUTH_NAME_MAX; i++) {
            d[k++] = domain[i];
        }
    }
    d[k] = 0;
    if (u[0] == 0) {
        log_msg(LOG_WARN, "login %s: no user name: refused", peer);
        return AUTH_BAD;
    }
    if (g_one_ready) {
        EnterCriticalSection(&g_one);
    }
    r = g_logon != NULL ? windows_check(u, d, pw, peer)
                        : own_check(u, pw, peer);
    if (r != AUTH_OK) {
        Sleep(AUTH_FAIL_MS);
    }
    if (g_one_ready) {
        LeaveCriticalSection(&g_one);
    }
    return r;
}

DWORD auth_set_account(const WCHAR *name, const WCHAR *pw)
{
    unsigned char salt[PWHASH_SALT];
    char hash[PWHASH_TEXT];
    HKEY key;
    DWORD disp, r;

    r = (DWORD) RegCreateKeyExA(HKEY_LOCAL_MACHINE, CONFIG_KEY, 0, "",
                                REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                                NULL, &key, &disp);
    if (r != ERROR_SUCCESS) {
        return r;
    }
    if (name[0] == 0) {
        RegDeleteValueA(key, AUTH_VALUE_USER);
        RegDeleteValueA(key, AUTH_VALUE_HASH);
        RegCloseKey(key);
        return 0;
    }
    pwhash_salt(salt);
    pwhash_make(pw, wlen(pw), salt, hash);
    r = (DWORD) RegSetValueExW(key, (LPWSTR) L"LoginUser", 0, REG_SZ,
                               (LPBYTE) name,
                               (wlen(name) + 1) * sizeof(WCHAR));
    if (r == ERROR_SUCCESS) {
        r = (DWORD) RegSetValueExA(key, AUTH_VALUE_HASH, 0, REG_SZ,
                                   (LPBYTE) hash, strlen(hash) + 1);
    }
    RegCloseKey(key);
    memset(hash, 0, sizeof(hash));
    return r;
}
