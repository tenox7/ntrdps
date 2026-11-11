/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <string.h>
#include "auth.h"

int t_auth_checks;
int t_auth_usable = 1;
int t_auth_result = -1;
char t_auth_user[AUTH_NAME_MAX * 2 + 2];

static int same(const WCHAR *w, const char *a)
{
    while (*a != '\0' && *w == (WCHAR) (unsigned char) *a) {
        w++;
        a++;
    }
    return *a == '\0' && *w == 0;
}

void auth_start(void)
{
}

int auth_windows(void)
{
    return 1;
}

int auth_usable(void)
{
    return t_auth_usable;
}

int auth_check(const WCHAR *user, const WCHAR *domain, const WCHAR *pw,
               const char *peer)
{
    t_auth_checks++;
    auth_log_name(t_auth_user, sizeof(t_auth_user), user);
    if (t_auth_result >= 0) {
        return t_auth_result;
    }
    if (!same(pw, "secret")) {
        return AUTH_BAD;
    }
    if (same(user, "admin")) {
        return AUTH_OK;
    }
    return same(user, "user") ? AUTH_NOT_ADMIN : AUTH_BAD;
}

DWORD auth_set_account(const WCHAR *name, const WCHAR *pw)
{
    return 0;
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
