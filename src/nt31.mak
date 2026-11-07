# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

CC        = cl386
CFLAGS    = -nologo -c -W3 -Ox -D_X86_=1 -DWIN32
CFLAGS_MT = $(CFLAGS) -D_MT
CFLAGS_DLL = $(CFLAGS) -Zl

LINK      = link32
LFLAGS    = -align:0x1000
IMPLIB    = lib32 -machine:i386

EXE_CON   = -subsystem:console -entry:mainCRTStartup
LIBS_MT   = libcmt.lib kernel32.lib

DLL_FLAGS = -dll -subsystem:windows -entry:DllMain@12

WINLIBS   = user32.lib gdi32.lib advapi32.lib wsock32.lib

!include ntrdps.mak
