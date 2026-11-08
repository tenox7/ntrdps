# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

DDKROOT  = C:\DDK

!IF "$(CPU)" == "MIPS"
CPUDEF   = -DMIPS=1 -D_MIPS_=1 -DMIPSEL -DR4000 -D_M_MRX000=4000 -Gt0 \
           -QMOb4000 -QMmips2
MACH     = mips
CPULINK  = -gpsize:0
!ENDIF
!IF "$(CPU)" == "ALPHA"
CPUDEF   = -DALPHA=1 -D_ALPHA_=1 -Gt0
MACH     = alpha
!ENDIF
!IF "$(CPU)" == "PPC"
CPUDEF   = -DPPC=1 -D_PPC_=1 -D_M_PPC=1
MACH     = ppc
INT64LIB = $(DDKROOT)\lib\ppc\free\int64.lib
!ENDIF
!IFNDEF MACH
!ERROR Give CPU=MIPS, CPU=ALPHA or CPU=PPC on the nmake command line.
!ENDIF

DDKLIB   = $(DDKROOT)\lib\$(MACH)\free

SDKINC   = $(MSTOOLS)\include

CC       = cl
CFLAGS   = -nologo -c -Zel -Zp8 -Gy -W3 -Ox -GX- -GR- -GF $(CPUDEF) \
           -DNO_EXT_KEYS -DCONDITION_HANDLING=1 -DNT_UP=1 -DNT_INST=0 \
           -DWIN32=100 -D_NT1X_=100 -DWINNT=1 -D_WIN32_WINNT=0x0400 \
           -DWIN32_LEAN_AND_MEAN=1 -DDEVL=1 -DFPO=1 -DNDEBUG -DDBG=0 \
           -D__stdcall= -D__cdecl= \
           -I. -I$(DDKROOT)\inc -I$(SDKINC) -FI$(DDKROOT)\inc\warning.h
CFLAGS_MINIPORT = $(CFLAGS) -I$(DDKROOT)\src\video\inc

LINK     = link
LFLAGS   = -nologo -machine:$(MACH) -opt:ref -incremental:no \
           -force:multiple -release -nodefaultlib \
           -debug:notmapped,minimal -debugtype:coff \
           -version:4.00 -osversion:4.00 -pdb:none -driver $(CPULINK)
IMPLIB   = lib -nologo -machine:$(MACH)
RC       = rc

GDIDRV_FLAGS = -dll -subsystem:native,4.00 -base:0x10000 \
               -entry:DrvEnableDriver
GDIDRV_LIBS  = $(DDKLIB)\win32k.lib $(DDKLIB)\libcntpr.lib $(INT64LIB)

MINIPORT_FLAGS = -subsystem:native,4.00 -base:0x10000 -entry:DriverEntry
MINIPORT_LIBS  = $(DDKLIB)\videoprt.lib $(DDKLIB)\libcntpr.lib $(INT64LIB)

!include mirror.mak
