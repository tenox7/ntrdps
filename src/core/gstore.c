/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <stdlib.h>
#include <string.h>
#include "gstore.h"

#define GSTORE_HASH 2048
#define NONE        (-1)

typedef struct {
    CORE_GLYPH    g;
    unsigned long hash;
    unsigned long serial;
    unsigned long used;
    int           next;
} GPLACE;

struct GSTORE {
    int           head[GSTORE_HASH];
    GPLACE        place[GSTORE_GLYPHS];
    unsigned long clock;
    unsigned int  filled;
    CORE_GLYPHREF log[CORE_GLYPH_LOG];
    unsigned long log_next;
    unsigned long log_kept;
};

GSTORE *gstore_new(void)
{
    GSTORE *s = (GSTORE *) calloc(1, sizeof(GSTORE));
    int i;

    if (s != NULL) {
        for (i = 0; i < GSTORE_HASH; i++) {
            s->head[i] = NONE;
        }
    }
    return s;
}

void gstore_free(GSTORE *g)
{
    free(g);
}

static unsigned long fnv(unsigned long h, unsigned long v, int bytes)
{
    int i;

    for (i = 0; i < bytes; i++) {
        h = ((h ^ (v & 0xFFUL)) * 16777619UL) & 0xFFFFFFFFUL;
        v >>= 8;
    }
    return h;
}

static unsigned long glyph_hash(const CORE_GLYPH *g)
{
    unsigned long h = 2166136261UL;
    unsigned int i, n = CORE_GLYPH_SIZE(g->cx, g->cy);

    h = fnv(h, (unsigned long) (long) g->ox, 4);
    h = fnv(h, (unsigned long) (long) g->oy, 4);
    h = fnv(h, (unsigned long) g->cx, 4);
    h = fnv(h, (unsigned long) g->cy, 4);
    for (i = 0; i < n; i++) {
        h = fnv(h, g->bits[i], 1);
    }
    return h;
}

static int same(const CORE_GLYPH *a, const CORE_GLYPH *b)
{
    return a->ox == b->ox && a->oy == b->oy && a->cx == b->cx &&
           a->cy == b->cy &&
           memcmp(a->bits, b->bits, CORE_GLYPH_SIZE(a->cx, a->cy)) == 0;
}

static void unlink_place(GSTORE *s, int k)
{
    int *p = &s->head[s->place[k].hash & (GSTORE_HASH - 1)];

    while (*p != NONE) {
        if (*p == k) {
            *p = s->place[k].next;
            return;
        }
        p = &s->place[*p].next;
    }
}

unsigned int gstore_put(GSTORE *s, const CORE_GLYPH *g,
                        unsigned long *serial)
{
    unsigned long h = glyph_hash(g), least;
    int k, *chain = &s->head[h & (GSTORE_HASH - 1)];
    unsigned int i;
    GPLACE *p;

    s->clock++;
    for (k = *chain; k != NONE; k = s->place[k].next) {
        p = &s->place[k];
        if (p->hash == h && same(&p->g, g)) {
            p->used = s->clock;
            *serial = p->serial;
            return (unsigned int) k;
        }
    }
    if (s->filled < GSTORE_GLYPHS) {
        k = (int) s->filled++;
    } else {
        k = 0;
        least = s->clock - s->place[0].used;
        for (i = 1; i < GSTORE_GLYPHS; i++) {
            if (s->clock - s->place[i].used > least) {
                least = s->clock - s->place[i].used;
                k = (int) i;
            }
        }
        unlink_place(s, k);
    }
    p = &s->place[k];
    p->g = *g;
    p->hash = h;
    p->serial++;
    if (p->serial == 0) {
        p->serial = 1;
    }
    p->used = s->clock;
    p->next = *chain;
    *chain = k;
    *serial = p->serial;
    return (unsigned int) k;
}

const CORE_GLYPH *gstore_get(const GSTORE *s, unsigned int id,
                             unsigned long serial)
{
    if (id >= GSTORE_GLYPHS || serial == 0 ||
        s->place[id].serial != serial) {
        return NULL;
    }
    return &s->place[id].g;
}

unsigned long gstore_log(GSTORE *s, const CORE_GLYPHREF *r, int n)
{
    unsigned long first = s->log_next;
    int i;

    for (i = 0; i < n; i++) {
        s->log[s->log_next % CORE_GLYPH_LOG] = r[i];
        s->log_next++;
        if (s->log_kept < CORE_GLYPH_LOG) {
            s->log_kept++;
        }
    }
    return first;
}

int gstore_run(const GSTORE *s, unsigned long run, int n,
               CORE_GLYPHREF *out)
{
    unsigned long back = s->log_next - run;
    int i;

    if (n <= 0 || back > s->log_kept || back < (unsigned long) n) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        out[i] = s->log[(run + (unsigned long) i) % CORE_GLYPH_LOG];
    }
    return 1;
}
