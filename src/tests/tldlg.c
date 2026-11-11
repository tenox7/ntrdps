/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include "ldlg.h"

LDLG *ldlg_open(unsigned int width, unsigned int height, const WCHAR *user,
                const WCHAR *domain, LDLG_WAKE wake, void *ctx,
                const char *peer)
{
    return NULL;
}

void ldlg_key(LDLG *d, unsigned int code, unsigned int prefix, int down)
{
}

void ldlg_locks(LDLG *d, unsigned int locks)
{
}

void ldlg_mouse(LDLG *d, int x, int y, unsigned int buttons)
{
}

int ldlg_event(LDLG *d, WCHAR *user, WCHAR *domain, WCHAR *pw)
{
    return LDLG_NONE;
}

void ldlg_fail(LDLG *d)
{
}

int ldlg_picture(LDLG *d, LDLG_DRAW draw, void *ctx)
{
    return 0;
}

void ldlg_background(unsigned char *rgb)
{
    rgb[0] = 0;
    rgb[1] = 128;
    rgb[2] = 128;
}

void ldlg_close(LDLG *d)
{
}
