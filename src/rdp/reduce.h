/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_REDUCE_H
#define NTRDPS_REDUCE_H

#include "core.h"

#define REDUCE_TABLE_SIZE 65536U

void reduce_palette(unsigned char pal[256][3]);

void reduce_table(unsigned char *t, const CORE_FRAME *f);

void reduce_pixels(unsigned char *d, const unsigned char *t,
                   const unsigned char *p, unsigned int n,
                   unsigned int bpp);

#endif
