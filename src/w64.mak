# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

CPUDEF     = -D_AXP64_=1 -D_ALPHA64_=1 -DALPHA=1 -DWIN64 -D_WIN64
MACH       = alpha64

CC         = cl
CFLAGS     = -nologo -c -W3 -Wp64 -Ap64 -Ox $(CPUDEF) -DWIN32 -D_WIN32
CFLAGS_MT  = $(CFLAGS) -MT -DNTRDPS_TLS -DNTRDPS_LOGIN
CFLAGS_DLL = $(CFLAGS) -Zl

TLS = 1
BRCFLAGS = -nologo -c -W0 -Od -Ap64 $(CPUDEF) -DWIN32 -D_WIN32 -MT
BRSTDINT = -Itls\bearssl\compat

LINK       = link
LFLAGS     = -nologo -release -machine:$(MACH)
IMPLIB     = lib -nologo -machine:$(MACH)

EXE_CON    = -subsystem:console,4.00
EXE_GUI    = -subsystem:windows,4.00
LIBS_MT    = libcmt.lib kernel32.lib

DLL_FLAGS  = -dll -nodefaultlib -subsystem:windows,4.00 -entry:DllMain
DLL_RTLIB  = libc.lib

WINLIBS    = user32.lib gdi32.lib advapi32.lib wsock32.lib

!include ntrdps.mak
