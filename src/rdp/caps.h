/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_CAPS_H
#define NTRDPS_CAPS_H

#include "stream.h"
#include "session.h"

#define CAPS_GENERAL   1
#define CAPS_BITMAP    2
#define CAPS_ORDER     3
#define CAPS_BMCACHE   4
#define CAPS_POINTER   8
#define CAPS_SHARE     9
#define CAPS_COLCACHE  10
#define CAPS_INPUT     13
#define CAPS_FONT      14
#define CAPS_BRUSH     15
#define CAPS_GLYPH     16
#define CAPS_OFFSCREEN 17
#define CAPS_BMCACHE2  19

#define CAPS_HAVE(c, type) ((((c)->have) >> (type)) & 1UL)

int caps_demand_active(SESSION *s);

void caps_confirm_active(SESSION *s, INS *in);

#endif
