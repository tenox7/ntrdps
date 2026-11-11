/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_AUTH_H
#define NTRDPS_AUTH_H

#include <windows.h>

#define AUTH_OK        0
#define AUTH_BAD       1
#define AUTH_NOT_ADMIN 2
#define AUTH_NO_USER   3
#define AUTH_ERROR     4

#define AUTH_FAIL_MS   2000

#define AUTH_NAME_MAX  64
#define AUTH_PW_MAX    256

#define AUTH_VALUE_USER "LoginUser"
#define AUTH_VALUE_HASH "LoginHash"

void auth_start(void);

int auth_windows(void);

int auth_usable(void);

int auth_check(const WCHAR *user, const WCHAR *domain, const WCHAR *pw,
               const char *peer);

DWORD auth_set_account(const WCHAR *name, const WCHAR *pw);

void auth_log_name(char *out, unsigned int cap, const WCHAR *name);

#endif
