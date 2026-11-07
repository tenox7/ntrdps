/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_LOG_H
#define NTRDPS_LOG_H

#define LOG_ERROR 1
#define LOG_WARN  2
#define LOG_INFO  3
#define LOG_DEBUG 4

int  log_open(const char *file, int to_console);
void log_close(void);

#define LOG_FILE_MAX (4UL * 1024UL * 1024UL)
void log_set_file_max(unsigned long bytes);

void log_set_level(int level);
int  log_get_level(void);

void log_msg(int level, const char *fmt, ...);

#define LOG_HEX_MAX 1024
void log_hex(const char *direction, const void *buf, unsigned int len);

#define LOG_HEX_LINE_MAX 96
int log_hex_line(char *out, unsigned int offset, const unsigned char *p,
                 unsigned int n);

const char *log_path(void);

double log_ms(void);

#endif
