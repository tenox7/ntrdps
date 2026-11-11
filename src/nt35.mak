# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

CC         = cl
CFLAGS     = -nologo -c -W3 -Ox -D_X86_=1 -DWIN32 -D_WIN32
CFLAGS_MT  = $(CFLAGS) -MT -DNTRDPS_TLS -DNTRDPS_LOGIN
CFLAGS_DLL = $(CFLAGS) -Zl

TLS = 1
BRCFLAGS = -nologo -c -W0 -Od -D_X86_=1 -DWIN32 -D_WIN32 -MT
BRSTDINT = -Itls\bearssl\compat

LINK       = link
LFLAGS     = -nologo
IMPLIB     = lib -nologo -machine:i386

EXE_CON    = -subsystem:console
LIBS_MT    = libcmt.lib kernel32.lib

DLL_FLAGS  = -dll -nodefaultlib -entry:DllMain@12

WINLIBS    = user32.lib gdi32.lib advapi32.lib wsock32.lib

!include ntrdps.mak
