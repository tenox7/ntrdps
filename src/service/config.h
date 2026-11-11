/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_CONFIG_H
#define NTRDPS_CONFIG_H

#include <windows.h>

#define CONFIG_KEY "SOFTWARE\\ntrdps"

#define CFG_PORT            0
#define CFG_CONSOLE_PROMPT  1
#define CFG_PROMPT_TIMEOUT  2
#define CFG_LOG_LEVEL       3
#define CFG_NEW_CLIENT      4
#define CFG_ALTGR_FAKE_CTRL 5
#define CFG_COMPRESSION     6
#define CFG_COMPRESSION_MAX 7
#ifdef NTRDPS_TLS
#define CFG_ALLOW_TLS       8
#ifdef NTRDPS_LOGIN
#define CFG_LOGIN           9
#define CFG_LOGIN_PLAIN     10
#define CFG_DWORDS          11
#else
#define CFG_DWORDS          9
#endif
#else
#define CFG_DWORDS          8
#endif
#define CFG_LOG_FILE        CFG_DWORDS

#define CFG_DEFAULT    0
#define CFG_SET        1
#define CFG_BAD_TYPE   2
#define CFG_BAD_VALUE  3

typedef struct {
    DWORD value[CFG_DWORDS];
    char  log_file[MAX_PATH];
    int   key_found;
    unsigned char how[CFG_DWORDS + 1];
    DWORD bad[CFG_DWORDS + 1];
} CONFIG;

void config_defaults(CONFIG *c);

int config_take(CONFIG *c, const char *name, DWORD type, const BYTE *data,
                DWORD len);

void config_read(CONFIG *c, HKEY root, const char *subkey);

void config_log(const CONFIG *c);

#endif
