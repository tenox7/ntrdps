/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_GLYPHS_H
#define NTRDPS_GLYPHS_H

#include "session.h"

void glyphs_reset(SESSION *s);

void glyphs_caps(SESSION *s, char *text);

int glyphs_text(SESSION *s, const ORDER_SINK *k, const CORE_ORDER *o,
                unsigned long num, unsigned char *out, unsigned int room,
                unsigned int *count, int *done, int *type);

#endif
