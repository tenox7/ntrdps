/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "log.h"

#define LOG_LINE_MAX 1024

#ifndef ERROR_FILENAME_EXCED_RANGE
#define ERROR_FILENAME_EXCED_RANGE 206
#endif

static CRITICAL_SECTION g_lock;
static int    g_ready = 0;
static HANDLE g_file = INVALID_HANDLE_VALUE;
static HANDLE g_console = INVALID_HANDLE_VALUE;
static int    g_level = LOG_INFO;
static char   g_path[MAX_PATH + 16] = "";
static unsigned long g_size;
static unsigned long g_file_max = LOG_FILE_MAX;

static int line_prefix(char *line, int level);

static const char *level_name(int level)
{
    switch (level) {
    case LOG_ERROR: return "ERROR";
    case LOG_WARN:  return "WARN ";
    case LOG_INFO:  return "INFO ";
    case LOG_DEBUG: return "DEBUG";
    }
    return "?    ";
}

int log_open(const char *file, int to_console)
{
    char path[MAX_PATH + 16];
    char *slash;
    DWORD n;

    g_path[0] = '\0';

    if (!g_ready) {
        InitializeCriticalSection(&g_lock);
        g_ready = 1;
    }
    if (to_console) {
        g_console = GetStdHandle(STD_OUTPUT_HANDLE);
    }
    if (file != NULL && file[0] != '\0') {
        if (strlen(file) >= sizeof(path)) {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            return 0;
        }
        strcpy(path, file);
    } else {
        n = GetModuleFileNameA(NULL, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) {
            return 0;
        }
        path[n] = '\0';
        slash = strrchr(path, '\\');
        strcpy(slash != NULL ? slash + 1 : path, "ntrdps.log");
    }
    g_file = CreateFileA(path, GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    g_size = SetFilePointer(g_file, 0, NULL, FILE_END);
    if (g_size == 0xFFFFFFFFUL) {
        g_size = 0;
    }
    strcpy(g_path, path);
    return 1;
}

void log_set_file_max(unsigned long bytes)
{
    g_file_max = bytes != 0 ? bytes : LOG_FILE_MAX;
}

void log_close(void)
{
    if (!g_ready) {
        return;
    }
    EnterCriticalSection(&g_lock);
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
    g_path[0] = '\0';
    g_console = INVALID_HANDLE_VALUE;
    LeaveCriticalSection(&g_lock);
}

void log_set_level(int level)
{
    g_level = level;
}

int log_get_level(void)
{
    return g_level;
}

double log_ms(void)
{
    LARGE_INTEGER f, c;

    if (QueryPerformanceFrequency(&f) && (f.LowPart != 0 || f.HighPart != 0)
        && QueryPerformanceCounter(&c)) {
        return ((double) c.HighPart * 4294967296.0 + (double) c.LowPart) *
               1000.0 /
               ((double) f.HighPart * 4294967296.0 + (double) f.LowPart);
    }
    return (double) GetTickCount();
}

const char *log_path(void)
{
    return g_path;
}

static void old_name(char *old)
{
    char *dot, *slash;

    strcpy(old, g_path);
    slash = strrchr(old, '\\');
    dot = strrchr(old, '.');
    if (dot == NULL || (slash != NULL && dot < slash)) {
        strcat(old, ".old");
    } else if (lstrcmpiA(dot, ".old") == 0) {
        strcpy(dot, ".bak");
    } else {
        strcpy(dot, ".old");
    }
}

static void turn_file(void)
{
    char old[MAX_PATH + 24];
    char line[LOG_LINE_MAX];
    DWORD written;
    int n, moved;

    old_name(old);
    CloseHandle(g_file);
    DeleteFileA(old);
    moved = MoveFileA(g_path, old);
    g_file = CreateFileA(g_path, GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }
    if (!moved) {
        SetFilePointer(g_file, 0, NULL, FILE_END);
        g_size = g_file_max - g_file_max / 8;
        return;
    }
    n = line_prefix(line, LOG_INFO);
    n += sprintf(line + n, "log continued; the older lines are in "
                 "%.300s\r\n", old);
    WriteFile(g_file, line, (DWORD) n, &written, NULL);
    g_size = (unsigned long) n;
}

static void write_line(const char *line, int n, int level)
{
    DWORD written;

    if (g_file != INVALID_HANDLE_VALUE &&
        g_size + (unsigned long) n > g_file_max) {
        turn_file();
    }
    if (g_file != INVALID_HANDLE_VALUE) {
        WriteFile(g_file, line, (DWORD) n, &written, NULL);
        g_size += (unsigned long) n;
    }
    if (g_console != INVALID_HANDLE_VALUE && level <= LOG_INFO) {
        WriteFile(g_console, line, (DWORD) n, &written, NULL);
    }
}

static int line_prefix(char *line, int level)
{
    SYSTEMTIME t;

    GetLocalTime(&t);
    return sprintf(line, "%04u-%02u-%02u %02u:%02u:%02u.%03u %4lx %s ",
                   (unsigned) t.wYear, (unsigned) t.wMonth, (unsigned) t.wDay,
                   (unsigned) t.wHour, (unsigned) t.wMinute,
                   (unsigned) t.wSecond, (unsigned) t.wMilliseconds,
                   (unsigned long) GetCurrentThreadId(), level_name(level));
}

void log_msg(int level, const char *fmt, ...)
{
    char line[LOG_LINE_MAX];
    va_list ap;
    int n, m;

    if (level > g_level) {
        return;
    }
    n = line_prefix(line, level);
    va_start(ap, fmt);
    m = _vsnprintf(line + n, sizeof(line) - n - 2, fmt, ap);
    va_end(ap);
    if (m < 0) {
        m = (int) sizeof(line) - n - 2;
    }
    n += m;
    line[n++] = '\r';
    line[n++] = '\n';

    if (g_ready) {
        EnterCriticalSection(&g_lock);
    }
    write_line(line, n, level);
    if (g_ready) {
        LeaveCriticalSection(&g_lock);
    }
}

int log_hex_line(char *out, unsigned int offset, const unsigned char *p,
                 unsigned int n)
{
    static const char hex[] = "0123456789abcdef";
    unsigned int i;
    int k;

    if (n > 16) {
        n = 16;
    }
    k = sprintf(out, "    %04x  ", offset);
    for (i = 0; i < 16; i++) {
        if (i == 8) {
            out[k++] = ' ';
        }
        if (i < n) {
            out[k++] = hex[p[i] >> 4];
            out[k++] = hex[p[i] & 15];
        } else {
            out[k++] = ' ';
            out[k++] = ' ';
        }
        out[k++] = ' ';
    }
    out[k++] = ' ';
    for (i = 0; i < n; i++) {
        out[k++] = (p[i] >= 0x20 && p[i] < 0x7F) ? (char) p[i] : '.';
    }
    out[k] = '\0';
    return k;
}

void log_hex(const char *direction, const void *buf, unsigned int len)
{
    const unsigned char *p = (const unsigned char *) buf;
    char line[LOG_HEX_LINE_MAX + 2];
    char dump[(LOG_HEX_MAX / 16 + 1) * (LOG_HEX_LINE_MAX + 2)];
    unsigned int off;
    DWORD written;
    int n, k = 0;

    if (g_level < LOG_DEBUG) {
        return;
    }
    for (off = 0; off < len && off < LOG_HEX_MAX; off += 16) {
        k += log_hex_line(dump + k, off, p + off, len - off);
        dump[k++] = '\r';
        dump[k++] = '\n';
    }
    if (len > LOG_HEX_MAX) {
        k += sprintf(dump + k, "    (%u more bytes not shown)\r\n",
                     len - LOG_HEX_MAX);
    }
    if (g_ready) {
        EnterCriticalSection(&g_lock);
    }
    n = line_prefix(line, LOG_DEBUG);
    n += sprintf(line + n, "%.20s %u bytes\r\n", direction, len);
    if (g_file != INVALID_HANDLE_VALUE &&
        g_size + (unsigned long) (n + k) > g_file_max) {
        turn_file();
    }
    write_line(line, n, LOG_DEBUG);
    if (g_file != INVALID_HANDLE_VALUE && k > 0) {
        WriteFile(g_file, dump, (DWORD) k, &written, NULL);
        g_size += (unsigned long) k;
    }
    if (g_ready) {
        LeaveCriticalSection(&g_lock);
    }
}
