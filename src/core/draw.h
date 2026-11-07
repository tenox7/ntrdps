/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_DRAW_H
#define NTRDPS_DRAW_H

#include "core.h"

#define DRAW_USES_P(r) ((((r) >> 4) & 0x0F) != ((r) & 0x0F))
#define DRAW_USES_S(r) ((((r) >> 2) & 0x33) != ((r) & 0x33))
#define DRAW_USES_D(r) ((((r) >> 1) & 0x55) != ((r) & 0x55))

unsigned int draw_rop3_of_rop2(unsigned int r2);

unsigned long draw_rop3(unsigned int rop, unsigned long p, unsigned long d);

unsigned long draw_rop3_sd(unsigned int rop, unsigned long s,
                           unsigned long d);

void draw_order(unsigned char *top, long stride, unsigned int bpp,
                const CORE_ORDER *o);

void draw_text(unsigned char *top, long stride, unsigned int bpp,
               const CORE_ORDER *o, const CORE_GLYPHREF *r,
               const CORE_GLYPH *const *g);

void draw_bitmap(unsigned char *top, long stride, unsigned int bpp,
                 const CORE_ORDER *o, const CORE_BITMAP *bm);

int draw_pat_bit(const CORE_ORDER *o, long x, long y);

int draw_pat_split(const unsigned char *top, long stride, unsigned int bpp,
                   const CORE_ORDER *o, int bands, CORE_ORDER *out, int max);

int draw_pat_rgb8(const unsigned char *top, long stride,
                  const CORE_ORDER *o, const unsigned char *pal);

#endif
