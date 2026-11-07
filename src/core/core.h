/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_CORE_H
#define NTRDPS_CORE_H

#include <windows.h>

#define CORE_VERSION 7

typedef struct {
    unsigned long        cb;
    unsigned int         width, height;
    unsigned int         bpp;
    unsigned long        red_mask;
    unsigned long        green_mask;
    unsigned long        blue_mask;
    const unsigned char *palette;
    const unsigned char *bits;
    long                 stride;
} CORE_FRAME;

#define CORE_ORDER_FILL 1
#define CORE_ORDER_COPY 2
#define CORE_ORDER_LINE 3
#define CORE_ORDER_TEXT 4
#define CORE_ORDER_SAVE 5
#define CORE_ORDER_RESTORE 6
#define CORE_ORDER_FREE 7
#define CORE_ORDER_BITMAP 8
#define CORE_ORDER_PATTERN 9

#define CORE_OF_RGB8 0x01

#define CORE_GLYPH_BYTES 128
#define CORE_GLYPH_SIZE(cx, cy) \
    ((unsigned int) (((cx) + 7) / 8) * (unsigned int) (cy))
#define CORE_TEXT_MAX   48
#define CORE_GLYPH_PLACES 1024

typedef struct {
    int           ox, oy;
    int           cx, cy;
    unsigned char bits[CORE_GLYPH_BYTES];
} CORE_GLYPH;

#define CORE_BITMAP_MAX    64
#define CORE_BITMAP_PLACES 512

typedef struct {
    int                  cx, cy;
    unsigned long        key[2];
    const unsigned char *bits;
    long                 stride;
} CORE_BITMAP;

typedef struct {
    unsigned int  id;
    unsigned long serial;
    int           x;
} CORE_GLYPHREF;

typedef struct {
    int           kind;
    RECT          rect;
    unsigned long color;
    unsigned int  rop;
    int           src_x, src_y;
    RECT          op;
    unsigned long bg;
    int           y;
    unsigned long run;
    int           nglyph;
    unsigned long save;
    unsigned int  bmp;
    unsigned long bmp_serial;
    unsigned char pat[8];
    int           pat_x, pat_y;
    unsigned int  flags;
} CORE_ORDER;

#define CORE_POINTER_MAX 32

typedef struct {
    unsigned long cb;
    unsigned long serial;
    int           x, y;
    unsigned int  width, height;
    unsigned int  hot_x, hot_y;
    unsigned char and_mask[CORE_POINTER_MAX][CORE_POINTER_MAX / 8];
    unsigned char color[CORE_POINTER_MAX][CORE_POINTER_MAX * 3];
} CORE_POINTER;

#define CORE_STOP_DENIED   1
#define CORE_STOP_REPLACED 2
#define CORE_STOP_BUSY     3

typedef struct {
    unsigned long cb, version;

    void (__cdecl *Damage)(void *fe, const RECT *rects, int count,
                           unsigned long at);
    void (__cdecl *Orders)(void *fe);
    void (__cdecl *FrameChanged)(void *fe);
    void (__cdecl *PointerShape)(void *fe);
    void (__cdecl *PointerPos)(void *fe, int x, int y);
    void (__cdecl *Stop)(void *fe, int why);
    void (__cdecl *Hold)(void *fe, int on);
} FRONTEND_API;

#define CORE_VIEW_ONLY 1

#define CORE_BUTTON_LEFT   1
#define CORE_BUTTON_RIGHT  2
#define CORE_BUTTON_MIDDLE 4

#define CORE_LOCK_SCROLL 1
#define CORE_LOCK_NUM    2
#define CORE_LOCK_CAPS   4

#define CORE_LOG_ERROR 1
#define CORE_LOG_WARN  2
#define CORE_LOG_INFO  3
#define CORE_LOG_DEBUG 4

typedef struct {
    unsigned long cb, version;

    void *(__cdecl *SessionOpen)(void *core, const FRONTEND_API *fe,
                                 void *fe_ctx, const char *peer,
                                 unsigned int flags);
    void  (__cdecl *SessionClose)(void *core, void *session);
    void  (__cdecl *SessionViewOnly)(void *core, void *session, int on);

    int   (__cdecl *FrameLock)(void *core, CORE_FRAME *f);
    void  (__cdecl *FrameUnlock)(void *core);

    int   (__cdecl *PointerGet)(void *core, CORE_POINTER *p);

    void  (__cdecl *Key)(void *core, void *session, unsigned int sc,
                         unsigned int prefix, int down);
    void  (__cdecl *KeyLocks)(void *core, void *session,
                              unsigned int locks);
    void  (__cdecl *Mouse)(void *core, void *session, int x, int y,
                           unsigned int buttons, int wheel);
    void  (__cdecl *SecureAttention)(void *core, void *session);

    void  (__cdecl *Log)(void *core, int level, const char *msg);

    int   (__cdecl *SessionReady)(void *core, void *session);

    int   (__cdecl *OrdersGet)(void *core, unsigned long *next,
                               CORE_ORDER *out, int max);

    int   (__cdecl *TextGet)(void *core, const CORE_ORDER *o,
                             CORE_GLYPHREF *refs);

    const CORE_GLYPH *(__cdecl *GlyphGet)(void *core, unsigned int id,
                                          unsigned long serial);

    const CORE_BITMAP *(__cdecl *BitmapGet)(void *core, unsigned int id,
                                            unsigned long serial);
} CORE_API;

#define CORE_ORDER_LOG 1024

#define CORE_GLYPH_LOG 8192

typedef struct {
    const char *name;
    int  (*start)(CORE_FRAME *f);
    void (*stop)(void);
} CORE_BACKEND;

extern const CORE_BACKEND testpat_backend;

void testpat_set_bpp(unsigned int bpp);

void testpat_set_mode(unsigned int width, unsigned int height,
                      unsigned int bpp);

extern const CORE_BACKEND poll_backend;

void poll_set_getdibits(int on);

void poll_set_nohints(int on);

void poll_set_nomirror(int on);

void poll_set_shadowcheck(int on);

void poll_set_noorders(int on);

void poll_set_notext(int on);

void poll_set_nobitmaps(int on);

void poll_set_nopatterns(int on);

void poll_input_hint(void);

extern const CORE_API core_api;

void *core_start(const CORE_BACKEND *backend);

void core_stop(void *core);

void core_set_prompt(unsigned int timeout_s);

void core_set_new_client(int refuse);

void core_damage(const RECT *rects, int count);

void core_frame_changed(void);

void core_frame_format(const CORE_FRAME *f);

void core_pointer_shape(const CORE_POINTER *p);

void core_pointer_pos(int x, int y);

void core_frame_enter(void);
void core_frame_leave(void);

void core_order(const CORE_ORDER *o);

int core_glyph_put(const CORE_GLYPH *g, unsigned long *serial);

const CORE_GLYPH *core_glyph_get(unsigned int id, unsigned long serial);

unsigned long core_text_put(const CORE_GLYPHREF *r, int n);

int core_bitmap_put(int cx, int cy, unsigned int bpp,
                    const unsigned char *bits, long stride,
                    unsigned long *serial);

#endif
