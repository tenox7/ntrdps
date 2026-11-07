/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_POINTER_H
#define NTRDPS_POINTER_H

#include "session.h"

#define POINTER_ECHO_MS 100

void pointer_reset(SESSION *s);

void pointer_changed(SESSION *s);

void pointer_moved(SESSION *s, int x, int y);

void pointer_pump(SESSION *s);

void pointer_done(SESSION *s);

#endif
