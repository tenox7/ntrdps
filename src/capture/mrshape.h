/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_MRSHAPE_H
#define NTRDPS_MRSHAPE_H

#include <windows.h>
#include "mrchan.h"
#include "core.h"

typedef struct MRSHAPE_FORMAT {
    int                  depth;
    unsigned long        red_mask;
    unsigned long        green_mask;
    unsigned long        blue_mask;
    const unsigned char *palette;
} MRSHAPE_FORMAT;

int mrshape_convert(const MR_PTROUT *s, const MRSHAPE_FORMAT *f,
                    CORE_POINTER *p, const char **why);

#endif
