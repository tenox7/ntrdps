/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_PALETTE_H
#define NTRDPS_PALETTE_H

extern const unsigned char vga_colors[16][3];

void palette_16(unsigned char pal[256][3], const unsigned char *colors);

int palette_capture(unsigned char pal[256][3]);

#endif
