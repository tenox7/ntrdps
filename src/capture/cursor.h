/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_CURSOR_H
#define NTRDPS_CURSOR_H

#include <windows.h>
#include "core.h"

void cursor_reset(void);

void cursor_check(HDC screen, const char *desktop);

void cursor_mirror(const CORE_POINTER *p);

#endif
