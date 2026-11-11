/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_LDLG_H
#define NTRDPS_LDLG_H

#include <windows.h>
#include "auth.h"

typedef struct LDLG_s LDLG;

#define LDLG_NONE   0
#define LDLG_SUBMIT 1
#define LDLG_CANCEL 2
#define LDLG_FAILED 3

#define LDLG_USER_MAX (2 * AUTH_NAME_MAX + 1)

typedef void (*LDLG_WAKE)(void *ctx);

typedef void (*LDLG_DRAW)(void *ctx, int x, int y, int w, int h,
                          const unsigned char *bgr, long stride);

LDLG *ldlg_open(unsigned int width, unsigned int height, const WCHAR *user,
                const WCHAR *domain, LDLG_WAKE wake, void *ctx,
                const char *peer);

void ldlg_key(LDLG *d, unsigned int code, unsigned int prefix, int down);
void ldlg_locks(LDLG *d, unsigned int locks);
void ldlg_mouse(LDLG *d, int x, int y, unsigned int buttons);

int ldlg_event(LDLG *d, WCHAR *user, WCHAR *domain, WCHAR *pw);

void ldlg_fail(LDLG *d);

int ldlg_picture(LDLG *d, LDLG_DRAW draw, void *ctx);

void ldlg_background(unsigned char *rgb);

void ldlg_close(LDLG *d);

#endif
