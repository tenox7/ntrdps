# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

!IF "$(CPU)" == "MIPS"
CPUDEF     = -D_MIPS_=1
CPUOPT     = -Og-
MACH       = mips
!ENDIF
!IF "$(CPU)" == "ALPHA"
CPUDEF     = -D_ALPHA_=1
MACH       = alpha
!ENDIF
!IF "$(CPU)" == "PPC"
CPUDEF     = -D_PPC_=1
MACH       = ppc
!ENDIF
!IFNDEF MACH
!ERROR Give CPU=MIPS, CPU=ALPHA or CPU=PPC on the nmake command line.
!ENDIF

CC         = cl
CFLAGS     = -nologo -c -W3 -Ox $(CPUOPT) $(CPUDEF) -DWIN32 -D_WIN32
CFLAGS_MT  = $(CFLAGS) -MT
CFLAGS_DLL = $(CFLAGS) -Zl

LINK       = link
LFLAGS     = -nologo -release
IMPLIB     = lib -nologo -machine:$(MACH)

EXE_CON    = -subsystem:console,3.10
EXE_GUI    = -subsystem:windows,3.10
LIBS_MT    = libcmt.lib kernel32.lib

DLL_FLAGS  = -dll -nodefaultlib -subsystem:windows,3.10 -entry:DllMain
DLL_RTLIB  = libc.lib

WINLIBS    = user32.lib gdi32.lib advapi32.lib wsock32.lib

!include ntrdps.mak
