# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

!IF "$(CPU)" == "MIPS"
CC        = mcl
CPUDEF    = -D_MIPS_=1
MACH      = mips
!ENDIF
!IF "$(CPU)" == "ALPHA"
CC        = claxp
CPUDEF    = -D_ALPHA_=1
MACH      = alpha
!ENDIF
!IFNDEF MACH
!ERROR Give CPU=MIPS or CPU=ALPHA on the nmake command line.
!ENDIF

CFLAGS    = -c -W3 -Od $(CPUDEF) -DWIN32
CFLAGS_MT = $(CFLAGS) -D_MT -DNTRDPS_TLS -DNTRDPS_LOGIN
CFLAGS_DLL = $(CFLAGS)

TLS = 1
BRCFLAGS = -c -W0 -Od $(CPUDEF) -DWIN32 -D_MT -DBR_NT31
BRSTDINT = -Itls\bearssl\nt31

LINK      = link32
LFLAGS    =
IMPLIB    = lib32 -machine:$(MACH)
CVTRES    = cvtres -$(CPU)

EXE_CON   = -subsystem:console -entry:mainCRTStartup
EXE_GUI   = -subsystem:windows -entry:WinMainCRTStartup
LIBS_MT   = libcmt.lib kernel32.lib

DLL_FLAGS = -dll -subsystem:windows -entry:DllMain
DLL_RTLIB = libc.lib

WINLIBS   = user32.lib gdi32.lib advapi32.lib wsock32.lib

!include setupb.mak
