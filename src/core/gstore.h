/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_GSTORE_H
#define NTRDPS_GSTORE_H

#include "core.h"

#define GSTORE_GLYPHS CORE_GLYPH_PLACES

typedef struct GSTORE GSTORE;

GSTORE *gstore_new(void);
void    gstore_free(GSTORE *g);

unsigned int gstore_put(GSTORE *s, const CORE_GLYPH *g,
                        unsigned long *serial);

const CORE_GLYPH *gstore_get(const GSTORE *s, unsigned int id,
                             unsigned long serial);

unsigned long gstore_log(GSTORE *s, const CORE_GLYPHREF *r, int n);

int gstore_run(const GSTORE *s, unsigned long run, int n,
               CORE_GLYPHREF *out);

#endif
