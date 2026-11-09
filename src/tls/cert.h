/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_TLS_CERT_H
#define NTRDPS_TLS_CERT_H
#include <stddef.h>
#include "bearssl.h"
extern const br_x509_certificate ntrdps_tls_chain[1];
extern const size_t ntrdps_tls_chain_len;
extern const br_ec_private_key ntrdps_tls_key;
#endif
