/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_SP3DDI_H
#define NTRDPS_SP3DDI_H

#ifndef GCAPS_LAYERED
#define GCAPS_LAYERED 0x08000000
#endif

typedef struct _ENG_EVENT *PEVENT;

typedef BOOL (APIENTRY *PFN_EVENTGONE)(PVOID pvGone);

HDRVOBJ APIENTRY EngMapEvent(HDEV hdev, HANDLE hEvent, PEVENT *ppEvent,
                             PFN_EVENTGONE pfnGone, PVOID pvGone);
BOOL    APIENTRY EngUnmapEvent(HDRVOBJ hdo);
BOOL    APIENTRY EngSetEvent(PEVENT pEvent);

#endif
