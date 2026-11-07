/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include "palette.h"

const unsigned char vga_colors[16][3] = {
    { 0x00, 0x00, 0x00 }, { 0x80, 0x00, 0x00 }, { 0x00, 0x80, 0x00 },
    { 0x80, 0x80, 0x00 }, { 0x00, 0x00, 0x80 }, { 0x80, 0x00, 0x80 },
    { 0x00, 0x80, 0x80 }, { 0x80, 0x80, 0x80 }, { 0xC0, 0xC0, 0xC0 },
    { 0xFF, 0x00, 0x00 }, { 0x00, 0xFF, 0x00 }, { 0xFF, 0xFF, 0x00 },
    { 0x00, 0x00, 0xFF }, { 0xFF, 0x00, 0xFF }, { 0x00, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0xFF }
};

void palette_16(unsigned char pal[256][3], const unsigned char *colors)
{
    int i, k;

    for (i = 0; i < 256; i++) {
        for (k = 0; k < 3; k++) {
            pal[i][k] = i < 16 ? colors[i * 3 + k]
                      : i >= 240 ? colors[(i - 240) * 3 + k] : 0;
        }
    }
}

int palette_capture(unsigned char pal[256][3])
{
    int i, j, n = 0;

    for (i = 10; i < 246; i++) {
        for (j = 0; j < 256; j = j == 9 ? 246 : j + 1) {
            if (pal[i][0] == pal[j][0] && pal[i][1] == pal[j][1] &&
                pal[i][2] == pal[j][2]) {
                pal[i][2] = (unsigned char) (pal[i][2] ^ 1);
                n++;
                break;
            }
        }
    }
    return n;
}
