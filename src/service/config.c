/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "config.h"

static const struct {
    const char *name;
    DWORD       def, min, max;
} g_dword[] = {
    { "Port",          3389,     1,         65535 },
    { "ConsolePrompt", 0,        0,         1 },
    { "PromptTimeout", 30,       1,         3600 },
    { "LogLevel",      LOG_INFO, LOG_ERROR, LOG_DEBUG },
    { "NewClient",     0,        0,         1 },
    { "AltGrFakeCtrl", 2,        0,         2 },
    { "Compression",   1,        0,         1 },
    { "CompressionMaxType", 1,   0,         1 }
};

typedef char cfg_table_complete[sizeof(g_dword) / sizeof(g_dword[0]) ==
                                CFG_DWORDS ? 1 : -1];

#define LOG_FILE_NAME "LogFile"

void config_defaults(CONFIG *c)
{
    int i;

    memset(c, 0, sizeof(*c));
    for (i = 0; i < CFG_DWORDS; i++) {
        c->value[i] = g_dword[i].def;
    }
}

static int refuse(CONFIG *c, int i, int how, DWORD what)
{
    c->how[i] = (unsigned char) how;
    c->bad[i] = what;
    return how;
}

static int take_log_file(CONFIG *c, DWORD type, const BYTE *data, DWORD len)
{
    if (type != REG_SZ) {
        return refuse(c, CFG_LOG_FILE, CFG_BAD_TYPE, type);
    }
    if (len > sizeof(c->log_file)) {
        return refuse(c, CFG_LOG_FILE, CFG_BAD_VALUE, len);
    }
    while (len > 0 && data[len - 1] == '\0') {
        len--;
    }
    if (len >= sizeof(c->log_file)) {
        return refuse(c, CFG_LOG_FILE, CFG_BAD_VALUE, len);
    }
    memcpy(c->log_file, data, len);
    c->log_file[len] = '\0';
    c->how[CFG_LOG_FILE] = len > 0 ? CFG_SET : CFG_DEFAULT;
    return c->how[CFG_LOG_FILE];
}

int config_take(CONFIG *c, const char *name, DWORD type, const BYTE *data,
                DWORD len)
{
    DWORD v;
    int i;

    if (strcmp(name, LOG_FILE_NAME) == 0) {
        return take_log_file(c, type, data, len);
    }
    for (i = 0; i < CFG_DWORDS; i++) {
        if (strcmp(name, g_dword[i].name) == 0) {
            break;
        }
    }
    if (i == CFG_DWORDS) {
        return CFG_DEFAULT;
    }
    if (type != REG_DWORD || len != sizeof(DWORD)) {
        return refuse(c, i, CFG_BAD_TYPE, type);
    }
    memcpy(&v, data, sizeof(v));
    if (v < g_dword[i].min || v > g_dword[i].max) {
        return refuse(c, i, CFG_BAD_VALUE, v);
    }
    c->value[i] = v;
    c->how[i] = CFG_SET;
    return CFG_SET;
}

static void read_one(CONFIG *c, HKEY key, const char *name)
{
    BYTE data[MAX_PATH + 16];
    DWORD type = REG_NONE, len = sizeof(data);
    LONG r;

    r = RegQueryValueExA(key, (LPSTR) name, NULL, &type, data, &len);
    if (r == ERROR_SUCCESS || r == ERROR_MORE_DATA) {
        config_take(c, name, type, data, len);
    }
}

void config_read(CONFIG *c, HKEY root, const char *subkey)
{
    HKEY key;
    int i;

    config_defaults(c);
    if (RegOpenKeyExA(root, subkey, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return;
    }
    c->key_found = 1;
    for (i = 0; i < CFG_DWORDS; i++) {
        read_one(c, key, g_dword[i].name);
    }
    read_one(c, key, LOG_FILE_NAME);
    RegCloseKey(key);
}

void config_log(const CONFIG *c)
{
    char line[512];
    int i, n;

    n = 0;
    for (i = 0; i < CFG_DWORDS; i++) {
        n += sprintf(line + n, "%s %lu%s, ", g_dword[i].name,
                     (unsigned long) c->value[i],
                     c->how[i] == CFG_SET ? " (set)" : "");
    }
    if (c->log_file[0] != '\0') {
        sprintf(line + n, "%s \"%.260s\" (set)", LOG_FILE_NAME, c->log_file);
    } else {
        sprintf(line + n, "%s none", LOG_FILE_NAME);
    }
    if (c->key_found) {
        log_msg(LOG_INFO, "settings from HKLM\\" CONFIG_KEY " (\"set\": "
                "the value is there): %s", line);
    } else {
        log_msg(LOG_INFO, "settings: no key HKLM\\" CONFIG_KEY ", so all "
                "defaults: %s", line);
    }

    for (i = 0; i <= CFG_DWORDS; i++) {
        const char *name = i < CFG_DWORDS ? g_dword[i].name : LOG_FILE_NAME;

        if (c->how[i] == CFG_BAD_TYPE) {
            log_msg(LOG_WARN, "settings: %s has type %lu, not %s: the "
                    "default is used", name, (unsigned long) c->bad[i],
                    i < CFG_DWORDS ? "REG_DWORD (4)" : "REG_SZ (1)");
        } else if (c->how[i] == CFG_BAD_VALUE && i < CFG_DWORDS) {
            log_msg(LOG_WARN, "settings: %s %lu is not allowed (%lu - %lu): "
                    "the default %lu is used", name, (unsigned long) c->bad[i],
                    (unsigned long) g_dword[i].min,
                    (unsigned long) g_dword[i].max,
                    (unsigned long) g_dword[i].def);
        } else if (c->how[i] == CFG_BAD_VALUE) {
            log_msg(LOG_WARN, "settings: %s is too long (%lu bytes or more; "
                    "at most %d characters): the default is used", name,
                    (unsigned long) c->bad[i], MAX_PATH - 1);
        }
    }
}
