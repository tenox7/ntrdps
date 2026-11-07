/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_MRCHAN_H
#define NTRDPS_MRCHAN_H

#define MR_ESCAPE       0x4E525053L
#define MR_MAGIC        0x524D524EL
#define MR_VERSION      7

#define MR_CMD_START    1
#define MR_CMD_GET      2
#define MR_CMD_STOP     3
#define MR_CMD_POINTER  4
#define MR_CMD_FLUSH    5

#define MR_FL_ORDERS    0x0001
#define MR_FL_TEXT      0x0002
#define MR_FL_BITMAPS   0x0004
#define MR_FL_PATTERNS  0x0008

#define MR_OK           0
#define MR_STOPPED      1
#define MR_BADEVENT     2
#define MR_BADCMD       3
#define MR_BUSY         4
#define MR_NOSHADOW     5
#define MR_NOSCREEN     6

#define MR_F_ALL        0x0001
#define MR_F_PALETTE    0x0002
#define MR_F_MODE       0x0004
#define MR_F_FULLSCREEN 0x0008
#define MR_F_POINTER    0x0010
#define MR_F_MORE       0x0020

#define MR_D_OWNSURF    0x0001
#define MR_D_GDIINFO    0x0002
#define MR_D_PALETTE    0x0004
#define MR_D_OWNPAL     0x0008
#define MR_D_SHADOW     0x0010
#define MR_D_LAYER      0x0020
#define MR_D_BMPSCREEN  0x0040
#define MR_D_NT31       0x0080
#define MR_D_NORES      0x0100
#define MR_D_NOEVENT    0x0200
#define MR_D_SWPOINTER  0x0400

#define MR_RECTS        64

typedef struct MR_IN {
    ULONG magic;
    ULONG version;
    ULONG cmd;
    ULONG arg;
    ULONG pid;
} MR_IN;

#define MR_IN_MIN       16

typedef struct MR_RECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} MR_RECT;

typedef struct MR_OUT {
    ULONG   magic;
    ULONG   version;
    ULONG   status;
    ULONG   flags;
    LONG    cx;
    LONG    cy;
    ULONG   bpp;
    ULONG   engine;
    ULONG   diag;
    ULONG   calls;
    ULONG   reads;
    ULONG   ptrshape;
    ULONG   ptrmove;
    ULONG   ptrflags;
    ULONG   ptrcolor;
    LONG    ptrcx;
    LONG    ptrcy;
    LONG    ptrhotx;
    LONG    ptrhoty;
    ULONG   nrects;
    MR_RECT rect[MR_RECTS];
} MR_OUT;

#define MR_O_FILL       1
#define MR_O_COPY       2
#define MR_O_LINE       3
#define MR_O_TEXT       4
#define MR_O_SAVE       5
#define MR_O_RESTORE    6
#define MR_O_BITMAP     7
#define MR_O_PATTERN    8

#define MR_SAVES        8
#define MR_ORDERS       256
#define MR_PLACES       4096
#define MR_GLYPHAREA    32768
#define MR_TEXT_MAX     256
#define MR_GLYPH_BITS   128
#define MR_BITMAPAREA   24576
#define MR_BITMAPS      1024
#define MR_BITMAP_MAX   64

typedef struct MR_ORDER {
    ULONG kind;
    LONG  left;
    LONG  top;
    LONG  right;
    LONG  bottom;
    LONG  sx;
    LONG  sy;
    ULONG color;
    ULONG rop;
    LONG  oleft;
    LONG  otop;
    LONG  oright;
    LONG  obottom;
    ULONG bg;
    LONG  y;
    ULONG first;
    ULONG nglyph;
} MR_ORDER;

typedef struct MR_GLYPHPOS {
    ULONG glyph;
    LONG  x;
} MR_GLYPHPOS;

typedef struct MR_GLYPH {
    LONG  ox;
    LONG  oy;
    ULONG cx;
    ULONG cy;
} MR_GLYPH;

#define MR_GLYPH_ROWB(cx)       (((ULONG) (cx) + 7) / 8)
#define MR_GLYPH_SIZE(cx, cy)   (sizeof(MR_GLYPH) + \
                                 ((MR_GLYPH_ROWB(cx) * (ULONG) (cy) + 3) & \
                                  ~3UL))

typedef struct MR_BITMAP {
    ULONG cx;
    ULONG cy;
} MR_BITMAP;

#define MR_BITMAP_SIZE(cx, cy, bpp) (sizeof(MR_BITMAP) + \
    ((MR_ROW_BYTES(0, cx, bpp) * (ULONG) (cy) + 3) & ~3UL))

#define MR_ROW_BYTES(l, r, bpp) ((ULONG) ((r) - (l)) * (ULONG) (bpp) / 8)

#define MR_RW_BMPCOPY   0
#define MR_RW_BMPROP    1
#define MR_RW_BMPBIG    2
#define MR_RW_BMPPAT    3
#define MR_RW_PATTERN   4
#define MR_RW_MASK      5
#define MR_RW_SHAPE     6
#define MR_RW_RESTORE   7
#define MR_RW_TEXT      8
#define MR_RW_FULL      9
#define MR_RW_OFF       10
#define MR_RW_OTHER     11
#define MR_RW_N         12

typedef struct MR_FLUSHOUT {
    MR_OUT  head;
    ULONG   datalen;
    ULONG   skipped;
    ULONG   saves;
    ULONG   savefail;
    ULONG   norders;
    ULONG   ordcalls;
    ULONG   ordmiss;
    ULONG   nplaces;
    ULONG   glyphlen;
    ULONG   textcalls;
    ULONG   textmiss;
    ULONG   textwhy[8];
    ULONG   rectwhy[MR_RW_N];
    ULONG   rectpix[MR_RW_N];
    ULONG   savenum[MR_SAVES];
    ULONG   saveords;
    ULONG   restords;
    ULONG   bitmaplen;
    ULONG   bmpcalls;
    ULONG   patcalls;
} MR_FLUSHOUT;

#define MR_TM_KIND      0
#define MR_TM_BATCH     1
#define MR_TM_GLYPH     2
#define MR_TM_PLACE     3
#define MR_TM_OPAQUE    4
#define MR_TM_FULL      5
#define MR_TM_PIECES    6
#define MR_TM_OTHER     7

#define MR_FLUSH_MIN    (sizeof(MR_FLUSHOUT) + MR_ORDERS * sizeof(MR_ORDER) \
                         + MR_PLACES * sizeof(MR_GLYPHPOS) + MR_GLYPHAREA \
                         + MR_BITMAPAREA + 16384)

#define MR_P_SHAPE      0x0001
#define MR_P_HIDDEN     0x0002
#define MR_P_COLOR      0x0004
#define MR_P_CUT        0x0008
#define MR_P_NOBITS     0x0010

#define MR_PTR_MAX      32

typedef struct MR_PTROUT {
    ULONG   magic;
    ULONG   version;
    ULONG   status;
    ULONG   serial;
    ULONG   flags;
    ULONG   fl;
    ULONG   format;
    ULONG   xlate;
    LONG    cx;
    LONG    cy;
    LONG    hotx;
    LONG    hoty;
    LONG    posx;
    LONG    posy;
    ULONG   and_mask[MR_PTR_MAX];
    ULONG   xor_mask[MR_PTR_MAX];
    ULONG   pix[MR_PTR_MAX][MR_PTR_MAX];
} MR_PTROUT;

#endif
