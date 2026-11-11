/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_DESKTOP_H
#define NTRDPS_DESKTOP_H

#include <windows.h>

typedef struct {
    const char *who;
    HANDLE      desk;
    HANDLE      first;
    char        name[64];
    DWORD       error;
#ifdef NTRDPS_LOGIN
    int         keep;
#endif
} DESKTOP_THREAD;

int desktop_init(void);

int desktop_follow(DESKTOP_THREAD *t);

int desktop_enter(DESKTOP_THREAD *t, const char *name);

#ifdef NTRDPS_LOGIN
int desktop_enter_own(DESKTOP_THREAD *t, const char *name);
#endif

void desktop_leave(DESKTOP_THREAD *t);

int desktop_input_name(char *name, unsigned int size);

int desktop_nt31(void);

int desktop_foreign(const char *name);

#endif
