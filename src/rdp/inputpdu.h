/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_INPUTPDU_H
#define NTRDPS_INPUTPDU_H

#include "stream.h"
#include "session.h"

#define INPUT_EVENT_SIZE 12

#define IN_SKIP   0
#define IN_SYNC   1
#define IN_KEY    2
#define IN_MOVE   3
#define IN_BUTTON 4
#define IN_WHEEL  5

typedef struct {
    int           kind;
    unsigned int  type;
    unsigned int  flags;
    unsigned long code;
    unsigned int  ext;
    int           up;
    int           x, y;
    int           wheel;
    const char   *why;
} INPUT_EVENT;

void input_decode(const unsigned char *ev, unsigned int w, unsigned int h,
                  INPUT_EVENT *e);

void input_pdu(SESSION *s, INS *in);

#endif
