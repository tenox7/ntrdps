/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <stddef.h>
#include <stdarg.h>
#include <windef.h>
#include <wingdi.h>
#include <winddi.h>

#include "sp3ddi.h"

HDRVOBJ APIENTRY EngMapEvent(HDEV hdev, HANDLE hEvent, PEVENT *ppEvent,
                             PFN_EVENTGONE pfnGone, PVOID pvGone)
{
    return NULL;
}

BOOL APIENTRY EngUnmapEvent(HDRVOBJ hdo)
{
    return FALSE;
}

BOOL APIENTRY EngSetEvent(PEVENT pEvent)
{
    return FALSE;
}
