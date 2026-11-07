/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_RECORD_H
#define NTRDPS_RECORD_H

#include "mrchan.h"

typedef struct REC_LIST {
    LONG    cx;
    LONG    cy;
    ULONG   flags;
    ULONG   n;
    ULONG   calls;
    MR_RECT r[MR_RECTS];
} REC_LIST;

void rec_init(REC_LIST *pl, LONG cx, LONG cy);

int rec_add(REC_LIST *pl, LONG left, LONG top, LONG right, LONG bottom);

int rec_flag(REC_LIST *pl, ULONG flags);

void rec_take(REC_LIST *pl, MR_OUT *po);

ULONG rec_flush(REC_LIST *pl, ULONG bpp, ULONG room, MR_RECT *out,
                ULONG *flags);

int rec_move(REC_LIST *pl, LONG l, LONG t, LONG r, LONG b, LONG dx,
             LONG dy);

int rec_bounds(const REC_LIST *pl, LONG l, LONG t, LONG r, LONG b,
               MR_RECT *out);

#endif
