/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_TLS_H
#define NTRDPS_TLS_H

#include <winsock.h>

typedef struct tls_conn TLS;

#define TLS_WOULDBLOCK (-2)

int  tls_startup(void);
void tls_cleanup(void);

void tls_random(void *out, unsigned int len);

TLS *tls_server_start(SOCKET sock);

int  tls_read(TLS *t, void *buf, int len);

int  tls_write(TLS *t, const void *buf, int len);

int  tls_flush(TLS *t);

long tls_events(TLS *t);

int  tls_pending(TLS *t);

const char *tls_error(TLS *t);

void tls_free(TLS *t);

#endif
