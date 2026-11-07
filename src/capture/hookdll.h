/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_HOOKDLL_H
#define NTRDPS_HOOKDLL_H

#include <windows.h>

void hookdll_start(void);

void hookdll_stop(void);

int hookdll_ready(void);

HCURSOR hookdll_cursor(LONG *serial, DWORD *thread);

int hookdll_hints(RECT *out, int max, int *all);

#endif
