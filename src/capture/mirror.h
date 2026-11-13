/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_MIRROR_H
#define NTRDPS_MIRROR_H

#include <windows.h>
#include "mrchan.h"

typedef struct MIRROR_STATS {
    unsigned long gets;
    unsigned long flushes;
    unsigned long rects;
    unsigned long bytes;
    unsigned long alls;
    unsigned long calls;
    unsigned long reads;
    unsigned long ptrshape;
    unsigned long ptrmove;
    unsigned long skipped;
    unsigned long saves;
    unsigned long savefail;
    unsigned long orders;
    unsigned long ordcalls;
    unsigned long ordmiss;
    unsigned long textcalls;
    unsigned long textmiss;
    unsigned long textwhy[8];
    unsigned long places;
    unsigned long glyphbytes;
    unsigned long rectwhy[MR_RW_N];
    unsigned long rectpix[MR_RW_N];
    unsigned long saveords;
    unsigned long restords;
    unsigned long bmpcalls;
    unsigned long bitmapbytes;
    unsigned long patcalls;
} MIRROR_STATS;

int mirror_open(void);

HANDLE mirror_event(void);

int mirror_layered(void);

int mirror_noevent(void);

int mirror_swpointer(void);

#ifdef NTRDPS_TLS
void mirror_fit(void);
#endif

int mirror_take(RECT *r, int max, unsigned long *flags);

int mirror_flush(MR_FLUSHOUT *f, unsigned long size, unsigned long fl);

#define mirror_flush_orders(f)  ((const MR_ORDER *) ((f) + 1))
#define mirror_flush_places(f) \
    ((const MR_GLYPHPOS *) (mirror_flush_orders(f) + (f)->norders))
#define mirror_flush_glyphs(f) \
    ((const unsigned char *) (mirror_flush_places(f) + (f)->nplaces))
#define mirror_flush_bitmaps(f) (mirror_flush_glyphs(f) + (f)->glyphlen)
#define mirror_flush_pixels(f)  (mirror_flush_bitmaps(f) + (f)->bitmaplen)

void mirror_stats(MIRROR_STATS *s);

int mirror_pointer(MR_PTROUT *s);

void mirror_close(void);

#endif
