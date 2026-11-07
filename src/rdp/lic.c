/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include "log.h"
#include "stream.h"
#include "session.h"
#include "mcs.h"
#include "sec.h"
#include "lic.h"

int lic_send(SESSION *s)
{
    unsigned char buf[64];
    OUTS o;

    mcs_sdin_begin(&o, buf, sizeof(buf));
    out_u16le(&o, SEC_LICENSE_PKT);
    out_u16le(&o, 0);
    out_u8(&o, 0xFF);
    out_u8(&o, 0x03);
    out_u16le(&o, 16);
    out_u32le(&o, 7);
    out_u32le(&o, 2);
    out_u16le(&o, 4);
    out_u16le(&o, 0);
    if (!mcs_sdin_end(s, &o)) {
        return 0;
    }
    s->licensed = 1;
    log_msg(LOG_INFO, "session %s: license sent (valid client)", s->peer);
    return 1;
}
