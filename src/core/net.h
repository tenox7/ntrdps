/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_NET_H
#define NTRDPS_NET_H

#include <windows.h>
#include <winsock.h>

#define NET_PEER_MAX     24

#define NET_SLOTS        8

#define NET_POLL_MS      100

#define NET_STOP_SERVICE  1

typedef void (*NET_SESSION_FN)(SOCKET sock, const char *peer,
                               volatile LONG *stop);

int net_serve(unsigned int port, HANDLE stop_event, NET_SESSION_FN fn);

const char *net_error_text(int err);

#endif
