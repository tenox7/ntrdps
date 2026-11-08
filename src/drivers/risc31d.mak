# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

DDKROOT  = C:\DDK

!IF "$(CPU)" == "MIPS"
CC       = mcl
CPUDEF   = -DMIPS=1 -D_MIPS_=1 -DMIPSEL -DR4000 -D_M_MRX000=4000
MACH     = mips
!ENDIF
!IF "$(CPU)" == "ALPHA"
CC       = claxp
CPUDEF   = -DALPHA=1 -D_ALPHA_=1
MACH     = alpha
!ENDIF
!IFNDEF MACH
!ERROR Give CPU=MIPS or CPU=ALPHA on the nmake command line.
!ENDIF

DDKLIB   = $(DDKROOT)\lib\$(MACH)\free

CFLAGS   = -c -Ze -W3 -Od $(CPUDEF) -DNO_EXT_KEYS \
           -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 -DNT_UP=1 \
           -DNT_INST=0 -DDBG=0 -DDEVL=1 -DBETA=0 -DNOFIREWALLS \
           -D__stdcall= -D__cdecl= -I. -I$(DDKROOT)\inc

LINK     = link32
LFLAGS   = -debug:minimal -debugtype:coff -machine:$(MACH)
IMPLIB   = lib32 -machine:$(MACH)
CVTRES   = cvtres -$(CPU)

DISPDLL_FLAGS = -dll -subsystem:native
DISPDLL_LIBS  = kernel32.lib $(DDKLIB)\winsrv.lib libc.lib

LAYER_DEF = layer\layer31.def
LAYER_RES = layer\layer.rbj
RCFLAGS   = -DLY_NT31
!include layer.mak
