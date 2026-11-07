/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_SHARE_H
#define NTRDPS_SHARE_H

#include "stream.h"
#include "session.h"

#define SHARE_VERSION       0x0010
#define SHARE_DEMAND_ACTIVE 1
#define SHARE_CONFIRM_ACTIVE 3
#define SHARE_DEACTIVATE_ALL 6
#define SHARE_DATA          7

#define T2_UPDATE          2
#define T2_CONTROL         20
#define T2_POINTER         27
#define T2_INPUT           28
#define T2_SYNCHRONIZE     31
#define T2_REFRESH_RECT    33
#define T2_SUPPRESS_OUTPUT 35
#define T2_SHUTDOWN_REQUEST 36
#define T2_FONTLIST        39
#define T2_FONTMAP         40
#define T2_PERSISTENT_LIST 43
#define T2_SET_ERROR_INFO  47

#define ERRINFO_RPC_INITIATED    1
#define ERRINFO_OTHER_CONNECTION 5
#define ERRINFO_OUT_OF_MEMORY    6
#define ERRINFO_DENIED           7

void share_pdus(SESSION *s, INS *in);

#define SHARE_DATA_HEADER 18
void share_begin(SESSION *s, OUTS *o, unsigned char *buf, unsigned int cap);
int  share_end(SESSION *s, OUTS *o, unsigned int type2);

int  share_deactivate_all(SESSION *s);

int  share_send_error_info(SESSION *s, unsigned long code);

void share_start_compression(SESSION *s, int asked);
void share_restart_compression(SESSION *s);
void share_done(SESSION *s);

#endif
