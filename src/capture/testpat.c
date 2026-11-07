/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "core.h"
#include "palette.h"

#define BLACK 0
#define WHITE 15

static const unsigned char bars[8] = { 0, 12, 10, 14, 9, 13, 11, 15 };

static unsigned int g_width = 1024, g_height = 768;
static unsigned int g_bpp = 8;
static unsigned char *g_bits;
static unsigned char g_palette[256][3];
static unsigned char *g_deep;
static int g_running;

static void draw(void)
{
    long w = (long) g_width, h = (long) g_height;
    long left = (w - 64) / 2, top = (h - 64) / 2;
    long x, y;
    unsigned char *p = g_bits;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (x >= left && x < left + 64 && y >= top && y < top + 64) {
                *p++ = ((x - left) / 8 + (y - top) / 8) % 2 ? WHITE : BLACK;
            } else if (x == 0 || x == w - 1 || y == 0 || y == h - 1) {
                *p++ = WHITE;
            } else {
                *p++ = bars[x * 8 / w];
            }
        }
    }
}

void testpat_set_bpp(unsigned int bpp)
{
    g_bpp = bpp;
}

static int deepen(CORE_FRAME *f)
{
    unsigned int b = g_bpp / 8, w;
    long i, n = (long) g_width * g_height;
    const unsigned char *c;
    unsigned char *p;

    g_deep = (unsigned char *) malloc((size_t) n * b);
    if (g_deep == NULL) {
        return 0;
    }
    for (i = 0, p = g_deep; i < n; i++, p += b) {
        c = g_palette[g_bits[i]];
        if (g_bpp == 16) {
            w = ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3);
            p[0] = (unsigned char) w;
            p[1] = (unsigned char) (w >> 8);
        } else {
            p[0] = c[2];
            p[1] = c[1];
            p[2] = c[0];
            if (g_bpp == 32) {
                p[3] = 0;
            }
        }
    }
    f->bpp = g_bpp;
    f->palette = NULL;
    f->bits = g_deep;
    f->stride = (long) g_width * b;
    if (g_bpp == 16) {
        f->red_mask = 0xF800;
        f->green_mask = 0x07E0;
        f->blue_mask = 0x001F;
    } else if (g_bpp == 32) {
        f->red_mask = 0xFF0000;
        f->green_mask = 0x00FF00;
        f->blue_mask = 0x0000FF;
    }
    return 1;
}

static void unmake(void)
{
    free(g_bits);
    g_bits = NULL;
    free(g_deep);
    g_deep = NULL;
}

static int make(CORE_FRAME *f)
{
    g_bits = (unsigned char *) malloc((size_t) g_width * g_height);
    if (g_bits == NULL) {
        log_msg(LOG_ERROR, "test pattern: out of memory");
        return 0;
    }
    draw();
    palette_16(g_palette, &vga_colors[0][0]);
    f->width = g_width;
    f->height = g_height;
    f->bpp = 8;
    f->palette = &g_palette[0][0];
    f->bits = g_bits;
    f->stride = (long) g_width;
    if (g_bpp != 8 && !deepen(f)) {
        log_msg(LOG_ERROR, "test pattern: out of memory");
        unmake();
        return 0;
    }
    return 1;
}

void testpat_set_mode(unsigned int width, unsigned int height,
                      unsigned int bpp)
{
    CORE_FRAME f;
    int ok;

    g_width = width;
    g_height = height;
    g_bpp = bpp;
    if (!g_running) {
        return;
    }
    memset(&f, 0, sizeof(f));
    core_frame_enter();
    unmake();
    ok = make(&f);
    core_frame_format(ok ? &f : NULL);
    core_frame_leave();
    core_frame_changed();
}

static int start(CORE_FRAME *f)
{
    g_running = make(f);
    return g_running;
}

static void stop(void)
{
    unmake();
    g_running = 0;
}

const CORE_BACKEND testpat_backend = { "test pattern", start, stop };
