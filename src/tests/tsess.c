/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "session.h"

void session_end(SESSION *s, int flush, const char *fmt, ...)
{
    va_list ap;

    if (s->ending != END_NO) {
        if (!flush) {
            s->ending = END_NOW;
        }
        return;
    }
    va_start(ap, fmt);
    _vsnprintf(s->why, sizeof(s->why) - 1, fmt, ap);
    va_end(ap);
    s->why[sizeof(s->why) - 1] = '\0';
    s->ending = flush ? END_FLUSH : END_NOW;
}

int session_send(SESSION *s, const unsigned char *pkt, unsigned int len)
{
    if (s->ending == END_NOW || s->tx_len + len > SESS_TX_MAX) {
        return 0;
    }
    memcpy(s->tx + s->tx_len, pkt, len);
    s->tx_len += len;
    s->tx_packets++;
    return 1;
}

void session_wake(SESSION *s)
{
}

#ifdef NTRDPS_TLS
int session_input_waiting(SESSION *s)
{
    return 0;
}
#endif
