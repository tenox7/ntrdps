/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_READER_H
#define NTRDPS_READER_H

#include <windows.h>

int reader_wanted(void);

unsigned char *reader_buffer(unsigned long bytes);

void reader_format(int w, int h, long stride, const void *dib,
                   unsigned int dib_len, int pal_dc);

int reader_select(const char *desktop);

int reader_area(int x0, int y0, int x1, int y1);
int reader_lines(int x0, int x1, const int *line, int n);

void reader_free(void);

int reader_main(const char *desktop, const char *pid, const char *serial);

#endif
