# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

DDKROOT  = C:\DDK

!IF "$(CPU)" == "MIPS"
CPUDEF   = -DMIPS=1 -D_MIPS_=1 -DMIPSEL -DR4000 -D_M_MRX000=4000 -Gt0 -QMOb4000
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

CC       = cl
CFLAGS   = -nologo -c -Zel -Zp8 -Gy -W3 -Ox $(CPUDEF) -DWINVER=0x030A \
           -DNO_EXT_KEYS -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 \
           -DNT_UP=1 -DNT_INST=0 -DWIN32=100 -D_NT1X_=100 -DDBG=0 \
           -DDEVL=1 -DFPO=1 -D__stdcall= -D__cdecl= -I. -I$(DDKROOT)\inc

LINK     = link
LFLAGS   = -nologo -machine:$(MACH) -opt:ref -release -incremental:no \
           -force:multiple -pdb:none -debug:notmapped,minimal \
           -version:3.51 -osversion:3.51 $(CPULINK)
IMPLIB   = lib -nologo -machine:$(MACH)

DISPDLL_FLAGS = -dll -noentry -subsystem:native,3.51
DISPDLL_LIBS  = kernel32.lib $(DDKLIB)\winsrv.lib libc.lib $(INT64LIB)

LAYER_DEF = layer\layer351.def
LAYER_RES = layer\layer.res
RCFLAGS   =
!include layer.mak
