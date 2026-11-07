/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_SEC_H
#define NTRDPS_SEC_H

#include "stream.h"
#include "session.h"

#define SEC_EXCHANGE_PKT 0x0001
#define SEC_ENCRYPT      0x0008
#define SEC_INFO_PKT     0x0040
#define SEC_LICENSE_PKT  0x0080

#define INFO_UNICODE       0x00000010UL
#define INFO_COMPRESSION   0x00000080UL
#define INFO_COMPR_TYPE(f) (((f) >> 9) & 0x0F)

int sec_client_info(SESSION *s, INS *in);

int sec_is_license(const INS *in);

void sec_hide_password(unsigned char *p, unsigned int len);

#endif
