/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_LOGIN_H
#define NTRDPS_LOGIN_H

#include "session.h"

#define LOGIN_GO     0
#define LOGIN_BOX    1
#define LOGIN_REFUSE 2

#define LOGIN_TRIES      3
#define LOGIN_TIMEOUT_MS 60000

int login_begin(SESSION *s, unsigned long *errinfo, const char **why);

void login_pump(SESSION *s);

void login_check_time(SESSION *s, DWORD now);

void login_free(SESSION *s);

#endif
