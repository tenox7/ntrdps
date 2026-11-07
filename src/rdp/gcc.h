/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_GCC_H
#define NTRDPS_GCC_H

#include "stream.h"
#include "session.h"

#define GCC_MAX_CHANNELS 42

unsigned int per_read_len(INS *in);

int gcc_read_request(SESSION *s, const unsigned char *p, unsigned int len);

unsigned int gcc_response_len(unsigned int n);
void gcc_write_response(OUTS *o, unsigned int n);

int gcc_session_depth(const SESSION *s, int d);

#endif
