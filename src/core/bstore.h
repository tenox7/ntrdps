/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_BSTORE_H
#define NTRDPS_BSTORE_H

#include "core.h"

#define BSTORE_PLACES CORE_BITMAP_PLACES
#define BSTORE_BYTES  (4UL << 20)

typedef struct BSTORE BSTORE;

BSTORE *bstore_new(void);
void    bstore_free(BSTORE *s);

void bstore_clear(BSTORE *s);

int bstore_put(BSTORE *s, int cx, int cy, unsigned int bpp,
               const unsigned char *bits, long stride,
               unsigned long *serial);

const CORE_BITMAP *bstore_get(const BSTORE *s, unsigned int id,
                              unsigned long serial);

void bstore_key(int cx, int cy, unsigned int bpp, const unsigned char *bits,
                long stride, unsigned long key[2]);

#endif
