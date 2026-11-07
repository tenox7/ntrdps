/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_BITMAPS_H
#define NTRDPS_BITMAPS_H

#include "session.h"
#include "orders.h"

void bitmaps_reset(SESSION *s);

void bitmaps_caps(SESSION *s, char *text);

int bitmaps_order(SESSION *s, const ORDER_SINK *k, const CORE_FRAME *f,
                  const CORE_ORDER *o, unsigned char *out, unsigned int room,
                  unsigned int *count, int *done);

#endif
