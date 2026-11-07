/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_MCS_H
#define NTRDPS_MCS_H

#include "stream.h"
#include "session.h"

#define MCS_SERVER_USER 1002
#define MCS_IO_CHANNEL  1003
#define MCS_USER_BASE   1001

#define MCS_RN_PROVIDER 1
#define MCS_RN_USER     3

void mcs_pdu(SESSION *s, INS *in);

int mcs_send_dpum(SESSION *s, unsigned int reason);

#define MCS_SDIN_MAX 0x3FFF
#define MCS_SDIN_HEADER 15
void mcs_sdin_begin(OUTS *o, unsigned char *buf, unsigned int cap);
int  mcs_sdin_end(SESSION *s, OUTS *o);

#endif
