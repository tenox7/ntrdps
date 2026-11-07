/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_RLE_H
#define NTRDPS_RLE_H

#define RLE_MAX_PIXELS 65535L

int rle_encode(const unsigned char *pix, int w, int h, int bpp,
               unsigned char *out, int max);

#endif
