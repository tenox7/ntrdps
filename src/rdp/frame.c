/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include "frame.h"

int frame_length(const unsigned char *p, unsigned int have, unsigned int *len)
{
    unsigned int n;

    *len = 0;
    if (have < 1) {
        return FRAME_MORE;
    }
    if (p[0] == 3) {
        if (have < 4) {
            return FRAME_MORE;
        }
        n = ((unsigned int) p[2] << 8) | p[3];
        if (n < FRAME_TPKT_MIN) {
            return FRAME_ERROR;
        }
        *len = n;
        return FRAME_TPKT;
    }
    if ((p[0] & 3) != 0) {
        return FRAME_ERROR;
    }
    if (have < 2) {
        return FRAME_MORE;
    }
    if (p[1] & 0x80) {
        if (have < 3) {
            return FRAME_MORE;
        }
        n = ((unsigned int) (p[1] & 0x7F) << 8) | p[2];
        if (n < 3) {
            return FRAME_ERROR;
        }
    } else {
        n = p[1];
        if (n < 2) {
            return FRAME_ERROR;
        }
    }
    *len = n;
    return FRAME_FASTPATH;
}
