/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "config.h"
#include "tcheck.h"

#define TEST_KEY  "Software\\ntrdps-t_config"
#define TEST_LOG  "t_config.log"

static int take_dword(CONFIG *c, const char *name, DWORD v)
{
    return config_take(c, name, REG_DWORD, (const BYTE *) &v, sizeof(v));
}

static int take_text(CONFIG *c, const char *name, const char *s, DWORD len)
{
    return config_take(c, name, REG_SZ, (const BYTE *) s, len);
}

static void test_defaults(void)
{
    CONFIG c;

    config_defaults(&c);
    CHECK_EQ(c.value[CFG_PORT], 3389);
    CHECK_EQ(c.value[CFG_CONSOLE_PROMPT], 0);
    CHECK_EQ(c.value[CFG_PROMPT_TIMEOUT], 30);
    CHECK_EQ(c.value[CFG_LOG_LEVEL], LOG_INFO);
    CHECK_EQ(c.value[CFG_NEW_CLIENT], 0);
    CHECK_EQ(c.value[CFG_ALTGR_FAKE_CTRL], 2);
    CHECK(c.log_file[0] == '\0');
    CHECK_EQ(c.key_found, 0);
}

static void test_take(void)
{
    static char long_name[MAX_PATH + 1];
    CONFIG c;
    DWORD v = 3390;

    config_defaults(&c);

    CHECK_EQ(take_dword(&c, "Port", 3390), CFG_SET);
    CHECK_EQ(c.value[CFG_PORT], 3390);
    CHECK_EQ(c.how[CFG_PORT], CFG_SET);
    CHECK_EQ(take_dword(&c, "Port", 65535), CFG_SET);
    CHECK_EQ(take_dword(&c, "Port", 1), CFG_SET);
    CHECK_EQ(c.value[CFG_PORT], 1);
    CHECK_EQ(take_dword(&c, "AltGrFakeCtrl", 0), CFG_SET);
    CHECK_EQ(c.value[CFG_ALTGR_FAKE_CTRL], 0);
    CHECK_EQ(take_dword(&c, "LogLevel", LOG_DEBUG), CFG_SET);
    CHECK_EQ(take_dword(&c, "PromptTimeout", 3600), CFG_SET);
    CHECK_EQ(take_dword(&c, "ConsolePrompt", 1), CFG_SET);
    CHECK_EQ(take_dword(&c, "NewClient", 1), CFG_SET);
    CHECK_EQ(c.value[CFG_NEW_CLIENT], 1);
    CHECK_EQ(take_dword(&c, "Compression", 0), CFG_SET);
    CHECK_EQ(c.value[CFG_COMPRESSION], 0);
    CHECK_EQ(take_dword(&c, "CompressionMaxType", 0), CFG_SET);
    CHECK_EQ(c.value[CFG_COMPRESSION_MAX], 0);

    config_defaults(&c);
    CHECK_EQ(take_dword(&c, "Port", 0), CFG_BAD_VALUE);
    CHECK_EQ(c.value[CFG_PORT], 3389);
    CHECK_EQ(c.bad[CFG_PORT], 0);
    CHECK_EQ(take_dword(&c, "Port", 65536), CFG_BAD_VALUE);
    CHECK_EQ(c.bad[CFG_PORT], 65536);
    CHECK_EQ(take_dword(&c, "LogLevel", 0), CFG_BAD_VALUE);
    CHECK_EQ(take_dword(&c, "LogLevel", 5), CFG_BAD_VALUE);
    CHECK_EQ(c.value[CFG_LOG_LEVEL], LOG_INFO);
    CHECK_EQ(take_dword(&c, "AltGrFakeCtrl", 3), CFG_BAD_VALUE);
    CHECK_EQ(take_dword(&c, "PromptTimeout", 0), CFG_BAD_VALUE);
    CHECK_EQ(take_dword(&c, "NewClient", 2), CFG_BAD_VALUE);
    CHECK_EQ(take_dword(&c, "Compression", 2), CFG_BAD_VALUE);
    CHECK_EQ(take_dword(&c, "CompressionMaxType", 3), CFG_BAD_VALUE);
    CHECK_EQ(c.value[CFG_COMPRESSION_MAX], 1);
    CHECK_EQ(take_dword(&c, "ConsolePrompt", 0xFFFFFFFF), CFG_BAD_VALUE);
    CHECK_EQ(c.value[CFG_CONSOLE_PROMPT], 0);

    CHECK_EQ(take_text(&c, "Port", "3390", 5), CFG_BAD_TYPE);
    CHECK_EQ(c.bad[CFG_PORT], REG_SZ);
    CHECK_EQ(c.value[CFG_PORT], 3389);
    CHECK_EQ(config_take(&c, "Port", REG_DWORD, (const BYTE *) &v, 2),
             CFG_BAD_TYPE);
    CHECK_EQ(config_take(&c, "LogFile", REG_DWORD, (const BYTE *) &v,
                         sizeof(v)), CFG_BAD_TYPE);
    CHECK_EQ(c.bad[CFG_LOG_FILE], REG_DWORD);
    CHECK(c.log_file[0] == '\0');

    CHECK_EQ(take_dword(&c, "Unknown", 1), CFG_DEFAULT);
    CHECK_EQ(take_dword(&c, "Por", 1), CFG_DEFAULT);
    CHECK_EQ(take_dword(&c, "PortX", 1), CFG_DEFAULT);
    CHECK_EQ(c.value[CFG_PORT], 3389);

    config_defaults(&c);
    CHECK_EQ(take_text(&c, "LogFile", "C:\\x\\a.log", 11), CFG_SET);
    CHECK(strcmp(c.log_file, "C:\\x\\a.log") == 0);
    CHECK_EQ(take_text(&c, "LogFile", "C:\\b.logZZZ", 8), CFG_SET);
    CHECK(strcmp(c.log_file, "C:\\b.log") == 0);
    CHECK_EQ(take_text(&c, "LogFile", "", 1), CFG_DEFAULT);
    CHECK(c.log_file[0] == '\0');
    CHECK_EQ(take_text(&c, "LogFile", "", 0), CFG_DEFAULT);

    memset(long_name, 'a', MAX_PATH);
    CHECK_EQ(take_text(&c, "LogFile", long_name, MAX_PATH - 1), CFG_SET);
    CHECK_EQ(strlen(c.log_file), MAX_PATH - 1);
    config_defaults(&c);
    CHECK_EQ(take_text(&c, "LogFile", long_name, MAX_PATH), CFG_BAD_VALUE);
    CHECK_EQ(c.bad[CFG_LOG_FILE], MAX_PATH);
    CHECK(c.log_file[0] == '\0');
    CHECK_EQ(take_text(&c, "LogFile", "", 100000), CFG_BAD_VALUE);
    CHECK_EQ(c.bad[CFG_LOG_FILE], 100000);
}

static void set_dword(HKEY key, const char *name, DWORD v)
{
    CHECK(RegSetValueExA(key, name, 0, REG_DWORD, (BYTE *) &v,
                         sizeof(v)) == ERROR_SUCCESS);
}

static void set_text(HKEY key, const char *name, const char *s)
{
    CHECK(RegSetValueExA(key, name, 0, REG_SZ, (BYTE *) s,
                         (DWORD) strlen(s) + 1) == ERROR_SUCCESS);
}

static void test_read(void)
{
    static char long_name[MAX_PATH + 32];
    CONFIG c;
    HKEY key;
    DWORD disp;

    RegDeleteKeyA(HKEY_CURRENT_USER, TEST_KEY);
    config_read(&c, HKEY_CURRENT_USER, TEST_KEY);
    CHECK_EQ(c.key_found, 0);
    CHECK_EQ(c.value[CFG_PORT], 3389);

    CHECK(RegCreateKeyExA(HKEY_CURRENT_USER, TEST_KEY, 0, NULL, 0,
                          KEY_ALL_ACCESS, NULL, &key, &disp) ==
          ERROR_SUCCESS);
    config_read(&c, HKEY_CURRENT_USER, TEST_KEY);
    CHECK_EQ(c.key_found, 1);
    CHECK_EQ(c.value[CFG_PORT], 3389);
    CHECK_EQ(c.how[CFG_PORT], CFG_DEFAULT);

    set_dword(key, "Port", 3390);
    set_dword(key, "AltGrFakeCtrl", 0);
    set_dword(key, "LogLevel", 9);
    set_text(key, "NewClient", "1");
    set_text(key, "LogFile", "C:\\ntrdps\\x.log");
    set_dword(key, "Other", 7);
    config_read(&c, HKEY_CURRENT_USER, TEST_KEY);
    CHECK_EQ(c.value[CFG_PORT], 3390);
    CHECK_EQ(c.how[CFG_PORT], CFG_SET);
    CHECK_EQ(c.value[CFG_ALTGR_FAKE_CTRL], 0);
    CHECK_EQ(c.value[CFG_LOG_LEVEL], LOG_INFO);
    CHECK_EQ(c.how[CFG_LOG_LEVEL], CFG_BAD_VALUE);
    CHECK_EQ(c.bad[CFG_LOG_LEVEL], 9);
    CHECK_EQ(c.value[CFG_NEW_CLIENT], 0);
    CHECK_EQ(c.how[CFG_NEW_CLIENT], CFG_BAD_TYPE);
    CHECK_EQ(c.bad[CFG_NEW_CLIENT], REG_SZ);
    CHECK(strcmp(c.log_file, "C:\\ntrdps\\x.log") == 0);
    CHECK_EQ(c.value[CFG_CONSOLE_PROMPT], 0);
    CHECK_EQ(c.how[CFG_CONSOLE_PROMPT], CFG_DEFAULT);

    memset(long_name, 'b', sizeof(long_name) - 1);
    set_text(key, "LogFile", long_name);
    config_read(&c, HKEY_CURRENT_USER, TEST_KEY);
    CHECK_EQ(c.how[CFG_LOG_FILE], CFG_BAD_VALUE);
    CHECK(c.log_file[0] == '\0');
    CHECK_EQ(c.value[CFG_PORT], 3390);

    RegDeleteValueA(key, "Port");
    RegDeleteValueA(key, "AltGrFakeCtrl");
    RegDeleteValueA(key, "LogLevel");
    RegDeleteValueA(key, "NewClient");
    RegDeleteValueA(key, "LogFile");
    RegDeleteValueA(key, "Other");
    RegCloseKey(key);
    CHECK(RegDeleteKeyA(HKEY_CURRENT_USER, TEST_KEY) == ERROR_SUCCESS);
}

static int read_line(const char *path, int want, char *buf, int size)
{
    FILE *f = fopen(path, "r");
    int i, len;

    buf[0] = '\0';
    if (f == NULL) {
        return 0;
    }
    for (i = 1; i <= want; i++) {
        if (fgets(buf, size, f) == NULL) {
            buf[0] = '\0';
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    len = (int) strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    return 1;
}

static int ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);

    return a >= b && strcmp(s + a - b, tail) == 0;
}

static const char want1[] =
    " INFO  settings: no key HKLM\\SOFTWARE\\ntrdps, so all defaults: "
    "Port 3389, ConsolePrompt 0, PromptTimeout 30, LogLevel 3, "
    "NewClient 0, AltGrFakeCtrl 2, Compression 1, CompressionMaxType 1, "
    "LogFile none";
static const char want2[] =
    " INFO  settings from HKLM\\SOFTWARE\\ntrdps (\"set\": the value is "
    "there): Port 3390 (set), ConsolePrompt 0, PromptTimeout 30, "
    "LogLevel 3, NewClient 0, AltGrFakeCtrl 2, Compression 1, "
    "CompressionMaxType 1, LogFile \"C:\\ntrdps\\x.log\" (set)";
static const char want3[] =
    " WARN  settings: LogLevel 9 is not allowed (1 - 4): the default 3 is "
    "used";
static const char want4[] =
    " WARN  settings: NewClient has type 1, not REG_DWORD (4): the default "
    "is used";
static const char want8[] =
    " WARN  settings: LogFile is too long (100000 bytes or more; at most "
    "259 characters): the default is used";

static void test_log(void)
{
    char buf[1024];
    CONFIG c;
    DWORD v = 1;

    DeleteFileA(TEST_LOG);
    CHECK(!log_open("no-such-folder\\" TEST_LOG, 0));
    CHECK(log_open(TEST_LOG, 0));
    CHECK(ends_with(log_path(), TEST_LOG));

    config_defaults(&c);
    config_log(&c);
    config_defaults(&c);
    c.key_found = 1;
    take_dword(&c, "Port", 3390);
    take_dword(&c, "LogLevel", 9);
    config_take(&c, "NewClient", REG_SZ, (const BYTE *) &v, 2);
    take_text(&c, "LogFile", "C:\\ntrdps\\x.log", 15);
    config_log(&c);
    take_text(&c, "LogFile", "", 100000);
    config_log(&c);
    log_close();

    CHECK(read_line(TEST_LOG, 1, buf, sizeof(buf)));
    CHECK(ends_with(buf, want1));
    CHECK(read_line(TEST_LOG, 2, buf, sizeof(buf)));
    CHECK(ends_with(buf, want2));
    CHECK(read_line(TEST_LOG, 3, buf, sizeof(buf)));
    CHECK(ends_with(buf, want3));
    CHECK(read_line(TEST_LOG, 4, buf, sizeof(buf)));
    CHECK(ends_with(buf, want4));
    CHECK(read_line(TEST_LOG, 5, buf, sizeof(buf)));
    CHECK(strstr(buf, " INFO  settings from ") != NULL);
    CHECK(read_line(TEST_LOG, 8, buf, sizeof(buf)));
    CHECK(ends_with(buf, want8));
    CHECK(!read_line(TEST_LOG, 9, buf, sizeof(buf)));
    printf("--- %s line 2:\n%s\n", TEST_LOG,
           read_line(TEST_LOG, 2, buf, sizeof(buf)) ? buf : "(none)");
    DeleteFileA(TEST_LOG);
}

int main(void)
{
    test_defaults();
    test_take();
    test_read();
    test_log();
    return t_result("t_config");
}
