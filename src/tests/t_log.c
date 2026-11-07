/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "tcheck.h"

static char *make_line(char *dst, const char *head, int n, const char *tail)
{
    strcpy(dst, head);
    memset(dst + strlen(head), ' ', n);
    strcpy(dst + strlen(head) + n, tail);
    return dst;
}

static const unsigned char pdu20[20] = {
    0x03, 0x00, 0x00, 0x14, 0x02, 0xF0, 0x80, 'n',
    't', 'r', 'd', 'p', 's', 0x00, 0x01, 0xFF,
    'a', 'b', 'c', 0x7F
};

static const char line0[] =
    "    0000  03 00 00 14 02 f0 80 6e  74 72 64 70 73 00 01 ff  .......ntrdps...";

static void test_lines(void)
{
    char out[LOG_HEX_LINE_MAX];
    char exp[LOG_HEX_LINE_MAX];
    static const unsigned char mixed[7] = {
        0x1F, 0x20, 0x41, 0x7E, 0x7F, 0x80, 0xFF
    };
    static const unsigned char x224[3] = { 0x03, 0x00, 0x0B };
    int n;

    n = log_hex_line(out, 0, (const unsigned char *) "0123456789abcdef", 16);
    CHECK(strcmp(out, "    0000  30 31 32 33 34 35 36 37  "
                      "38 39 61 62 63 64 65 66  0123456789abcdef") == 0);
    CHECK_EQ(n, strlen(out));

    n = log_hex_line(out, 0x10, x224, 3);
    make_line(exp, "    0010  03 00 0b", 42, "...");
    CHECK(strcmp(out, exp) == 0);
    CHECK_EQ(n, strlen(exp));

    log_hex_line(out, 0x1234, mixed, 7);
    make_line(exp, "    1234  1f 20 41 7e 7f 80 ff", 30, ". A~...");
    CHECK(strcmp(out, exp) == 0);

    log_hex_line(out, 0, pdu20, 20);
    CHECK(strcmp(out, line0) == 0);
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

static void test_file(void)
{
    char path[MAX_PATH + 16];
    char buf[256];
    char exp[LOG_HEX_LINE_MAX];

    CHECK(log_open(NULL, 1));
    strcpy(path, log_path());
    CHECK(ends_with(path, "ntrdps.log"));
    log_close();
    DeleteFileA(path);

    CHECK(log_open(NULL, 1));
    printf("--- the console's part of the log (the INFO line only):\n");
    fflush(stdout);
    log_set_level(LOG_DEBUG);
    log_hex("in", pdu20, 20);
    log_set_level(LOG_INFO);
    log_hex("out", pdu20, 20);
    log_msg(LOG_DEBUG, "dropped too");
    log_msg(LOG_INFO, "end of t_log");
    log_close();
    printf("--- end of the console's part\n");
    fflush(stdout);

    CHECK(read_line(path, 1, buf, sizeof(buf)));
    CHECK(ends_with(buf, " DEBUG in 20 bytes"));
    CHECK(read_line(path, 2, buf, sizeof(buf)));
    CHECK(strcmp(buf, line0) == 0);
    CHECK(read_line(path, 3, buf, sizeof(buf)));
    make_line(exp, "    0010  61 62 63 7f", 39, "abc.");
    CHECK(strcmp(buf, exp) == 0);
    CHECK(read_line(path, 4, buf, sizeof(buf)));
    CHECK(ends_with(buf, " INFO  end of t_log"));
    CHECK(!read_line(path, 5, buf, sizeof(buf)));
}

static void test_cap(void)
{
    static unsigned char big[LOG_HEX_MAX + 5];
    char path[MAX_PATH + 16];
    char buf[256];
    char exp[LOG_HEX_LINE_MAX];
    unsigned int i;

    for (i = 0; i < sizeof(big); i++) {
        big[i] = (unsigned char) i;
    }
    CHECK(log_open(NULL, 0));
    strcpy(path, log_path());
    log_set_level(LOG_DEBUG);
    log_hex("out", big, sizeof(big));
    log_set_level(LOG_INFO);
    log_close();

    CHECK(read_line(path, 5, buf, sizeof(buf)));
    sprintf(exp, " DEBUG out %u bytes", (unsigned int) sizeof(big));
    CHECK(ends_with(buf, exp));
    CHECK(read_line(path, 6, buf, sizeof(buf)));
    log_hex_line(exp, 0, big, 16);
    CHECK(strcmp(buf, exp) == 0);
    CHECK(read_line(path, 5 + LOG_HEX_MAX / 16, buf, sizeof(buf)));
    log_hex_line(exp, LOG_HEX_MAX - 16, big + LOG_HEX_MAX - 16, 16);
    CHECK(strcmp(buf, exp) == 0);
    CHECK(read_line(path, 6 + LOG_HEX_MAX / 16, buf, sizeof(buf)));
    CHECK(strcmp(buf, "    (5 more bytes not shown)") == 0);
    CHECK(!read_line(path, 7 + LOG_HEX_MAX / 16, buf, sizeof(buf)));
}

static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;

    if (f == NULL) {
        return -1;
    }
    fseek(f, 0L, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}

static int count_lines(const char *path, char *buf)
{
    char line[256];
    int n = 0;

    buf[0] = '\0';
    while (read_line(path, n + 1, line, sizeof(line))) {
        strcpy(buf, line);
        n++;
    }
    return n;
}

static void test_turn(void)
{
    static unsigned char pdu64[64];
    char path[MAX_PATH + 16], old[MAX_PATH + 24];
    char buf[256], exp[80];
    int i, n_old, n_new;

    CHECK(log_open(NULL, 0));
    strcpy(path, log_path());
    log_close();
    CHECK(ends_with(path, "ntrdps.log"));
    strcpy(old, path);
    strcpy(old + strlen(old) - 3, "old");
    DeleteFileA(path);
    DeleteFileA(old);

    log_set_file_max(2000);
    CHECK(log_open(NULL, 0));
    for (i = 0; i < 40; i++) {
        log_msg(LOG_INFO, "line %02d of the size test: 0123456789012345678",
                i);
    }
    log_close();
    CHECK(file_size(old) > 1000 && file_size(old) <= 2000);
    CHECK(file_size(path) > 0 && file_size(path) <= 2000);
    CHECK(read_line(old, 1, buf, sizeof(buf)));
    CHECK(ends_with(buf, " INFO  line 00 of the size test: "
                    "0123456789012345678"));
    n_old = count_lines(old, buf);
    n_new = count_lines(path, buf);
    CHECK(ends_with(buf, " INFO  line 39 of the size test: "
                    "0123456789012345678"));
    CHECK_EQ(n_old + n_new, 41);
    CHECK(read_line(path, 1, buf, sizeof(buf)));
    CHECK(strstr(buf, " INFO  log continued; the older lines are in ")
          != NULL);
    CHECK(ends_with(buf, "ntrdps.old"));
    CHECK(read_line(path, 2, buf, sizeof(buf)));
    sprintf(exp, " INFO  line %02d of the size test: 0123456789012345678",
            n_old);
    CHECK(ends_with(buf, exp));

    DeleteFileA(path);
    DeleteFileA(old);
    for (i = 0; i < 64; i++) {
        pdu64[i] = (unsigned char) i;
    }
    CHECK(log_open(NULL, 0));
    for (i = 0; file_size(path) < 1750 && i < 100; i++) {
        log_msg(LOG_INFO, "line %02d of the dump test", i);
    }
    CHECK_EQ(file_size(old), -1L);
    log_set_level(LOG_DEBUG);
    log_hex("out", pdu64, 64);
    log_set_level(LOG_INFO);
    log_close();
    log_set_file_max(0);
    CHECK(file_size(old) >= 1750 && file_size(old) <= 2000);
    CHECK(read_line(path, 2, buf, sizeof(buf)));
    CHECK(ends_with(buf, " DEBUG out 64 bytes"));
    CHECK(read_line(path, 6, buf, sizeof(buf)));
    CHECK(strncmp(buf, "    0030  30 31 32 33", 21) == 0);
    CHECK(!read_line(path, 7, buf, sizeof(buf)));
    DeleteFileA(old);
}

int main(void)
{
    test_lines();
    test_file();
    test_cap();
    test_turn();
    return t_result("t_log");
}
