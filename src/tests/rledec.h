/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_RLEDEC_H
#define NTRDPS_RLEDEC_H

#define RLEDEC_MS 0
#define RLEDEC_RD 1

#define RLEDEC_BAD       0
#define RLEDEC_BG        1
#define RLEDEC_FG        2
#define RLEDEC_SETFG     3
#define RLEDEC_FGBG      4
#define RLEDEC_SETFGBG   5
#define RLEDEC_COLOR     6
#define RLEDEC_IMAGE     7
#define RLEDEC_DITHER    8
#define RLEDEC_SPECIAL_1 9
#define RLEDEC_SPECIAL_2 10
#define RLEDEC_WHITE     11
#define RLEDEC_BLACK     12

int rledec_header(const unsigned char *in, long len, long *i, long *n);

int rledec(const unsigned char *in, long len, int w, int h, int bpp,
           int mode, unsigned char *out);

#endif
