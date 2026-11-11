/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_RDP_H
#define NTRDPS_RDP_H

#include "stream.h"
#include "session.h"

void rdp_start(const CORE_API *core, void *core_ctx);

void rdp_set_compression(int on, int max_type);
int  rdp_compression(int *max_type);

#ifdef NTRDPS_TLS
void rdp_set_allow_tls(int on);
int  rdp_allow_tls(void);
#endif

#ifdef NTRDPS_LOGIN
void rdp_set_login(int on, int plain);
int  rdp_login_on(void);
int  rdp_login_plain(void);

void rdp_take_mode(SESSION *s, const CORE_FRAME *f);

void rdp_console(SESSION *s, unsigned int locks);
#endif

int rdp_frame_depth(const CORE_FRAME *f);

int rdp_same_mode(const SESSION *s, const CORE_FRAME *f);

void rdp_take_damage(SESSION *s);

void rdp_init(SESSION *s);

void rdp_done(SESSION *s);

void rdp_packet(SESSION *s, int kind, unsigned char *p, unsigned int len);

void rdp_data(SESSION *s, INS *in);

void rdp_pump(SESSION *s);

void rdp_end(SESSION *s, unsigned long errinfo, const char *why);

void rdp_service_stop(SESSION *s);

#define RDP_HANDSHAKE_MS 10000
void rdp_check_time(SESSION *s, DWORD now);

const char *rdp_state_name(int state);

#endif
