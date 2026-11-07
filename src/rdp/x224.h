/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_X224_H
#define NTRDPS_X224_H

#include "stream.h"
#include "session.h"

#define X224_HEADER 7

void x224_connect(SESSION *s, const unsigned char *p, unsigned int len);

int x224_data(SESSION *s, const unsigned char *p, unsigned int len,
              INS *mcs);

void x224_begin(OUTS *o, unsigned char *buf, unsigned int cap);
int  x224_end(SESSION *s, OUTS *o);

#endif
