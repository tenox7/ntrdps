/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_UPDATE_H
#define NTRDPS_UPDATE_H

#include "session.h"

#define UPDATE_QUEUE_MAX 16384

void update_screen(SESSION *s);

int update_area(SESSION *s, long left, long top, long right, long bottom);

int update_damage(SESSION *s, const RECT *r, unsigned long at);

void update_stop(SESSION *s);

void update_pump(SESSION *s);

void update_done(SESSION *s);

unsigned int update_row(unsigned char *d, const CORE_FRAME *f,
                        const unsigned char *table, unsigned int x,
                        unsigned int y, unsigned int n);

#endif
