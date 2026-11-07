/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_FRAME_H
#define NTRDPS_FRAME_H

#define FRAME_TPKT_MIN 7
#define FRAME_MAX      65535

#define FRAME_ERROR    (-1)
#define FRAME_MORE     0
#define FRAME_TPKT     1
#define FRAME_FASTPATH 2

int frame_length(const unsigned char *p, unsigned int have, unsigned int *len);

#endif
