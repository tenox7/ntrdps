/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_INPUT_H
#define NTRDPS_INPUT_H

int input_start(int altgr_fake_ctrl);

void input_stop(void);

void input_key(unsigned int code, unsigned int prefix, int down);

void input_locks(unsigned int locks);

void input_mouse(int x, int y, unsigned int buttons, int wheel);

void input_release(void);

#endif
