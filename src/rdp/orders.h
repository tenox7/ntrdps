/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_ORDERS_H
#define NTRDPS_ORDERS_H

#include "core.h"

#define ORD_DSTBLT      0x00
#define ORD_PATBLT      0x01
#define ORD_SCRBLT      0x02
#define ORD_LINETO      0x09
#define ORD_OPAQUERECT  0x0A
#define ORD_SAVEBITMAP  0x0B
#define ORD_MEMBLT      0x0D
#define ORD_FASTINDEX   0x13
#define ORD_GLYPHINDEX  0x1B

#define ORD_TYPES       9
#define ORD_VALUES      23
#define ORD_TEXT_MAX    48

#define ORD_A_DSTBLT     0x01
#define ORD_A_PATBLT     0x02
#define ORD_A_SCRBLT     0x04
#define ORD_A_LINETO     0x08
#define ORD_A_OPAQUERECT 0x10
#define ORD_A_FASTINDEX  0x20
#define ORD_A_GLYPHINDEX 0x40
#define ORD_A_SAVEBITMAP 0x80
#define ORD_A_MEMBLT     0x100

typedef struct {
    int           last_type;
    int           fresh;
    long          bounds[4];
    unsigned long prev[ORD_TYPES][ORD_VALUES];
    unsigned char data[2][255];
    unsigned int  data_len[2];
} ORDER_ENC;

typedef struct {
    unsigned int         allow;
    int                  depth;
    unsigned int         frame_bpp;
    const unsigned char *reduce;
    int                  invert_bw;
    int                  brush_dy;
} ORDER_SINK;

void order_reset(ORDER_ENC *e);

unsigned int order_encode(ORDER_ENC *e, int type, const unsigned long *v,
                          const long *bounds, unsigned char *out,
                          unsigned int room);

unsigned int order_encode_var(ORDER_ENC *e, int type, const unsigned long *v,
                              const unsigned char *data, unsigned int len,
                              const long *bounds, unsigned char *out,
                              unsigned int room);

unsigned int order_allowed(unsigned int order_flags,
                           const unsigned char *support, int depth);

void order_allowed_text(unsigned int allow, char *text);

int order_core(ORDER_ENC *e, const ORDER_SINK *k, const CORE_ORDER *o,
               unsigned char *out, unsigned int room, int *type);

int order_reads(const CORE_ORDER *o, RECT *r);

#define ORD_SAVE_PELS(w, h) \
    ((unsigned long) (w) * (((unsigned long) (h) + 19) / 20 * 20))
#define ORD_SAVE_MAX 230400UL

unsigned int order_savebitmap(ORDER_ENC *e, unsigned long pos,
                              const RECT *r, int op, unsigned char *out,
                              unsigned int room);

unsigned int order_memblt(ORDER_ENC *e, int cache, unsigned int slot,
                          const RECT *r, long sx, long sy, unsigned int rop,
                          unsigned char *out, unsigned int room);

unsigned int order_cache_bitmap(int cache, unsigned int slot, unsigned int w,
                                unsigned int h, int depth, int rle, int hdr,
                                const unsigned char *data, unsigned int len,
                                unsigned char *out, unsigned int room);

#define ORD_COLORTABLE_BYTES 1033
unsigned int order_cache_colortable(const unsigned char pal[256][3],
                                    unsigned char *out, unsigned int room);

typedef struct {
    int           cache;
    int           n;
    unsigned char slot[ORD_TEXT_MAX];
    long          x[ORD_TEXT_MAX];
    long          y;
    long          bk[4];
    long          op[4];
    unsigned long fg;
    unsigned long bg;
} ORDER_TEXT;

int order_text(ORDER_ENC *e, const ORDER_SINK *k, const ORDER_TEXT *t,
               const long *bounds, unsigned char *out, unsigned int room,
               int *type);

#define ORD_GLYPH_BYTES(cx, cy) \
    ((((unsigned int) (cx) + 7) / 8 * (unsigned int) (cy) + 3) & ~3U)

unsigned int order_cache_glyph(int cache, int n, const unsigned char *slot,
                               const CORE_GLYPH *const *g,
                               unsigned char *out, unsigned int room);

#endif
