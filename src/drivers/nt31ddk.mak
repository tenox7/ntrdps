# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

DDKROOT  = C:\DDK
DDKLIB   = $(DDKROOT)\lib\i386\free

CC       = cl386
CFLAGS   = -nologo -c -Zel -Zp8 -W3 -Gz -H63 -G4 -Ox -Oy \
           -D_X86_=1 -Di386=1 -DSTD_CALL -DCONDITION_HANDLING=1 \
           -DWIN32_LEAN_AND_MEAN=1 -DNT_UP=1 -DNT_INST=0 \
           -DDBG=0 -DDEVL=1 -DBETA=0 -DNOFIREWALLS -DFPO=1 \
           -I. -I$(DDKROOT)\inc

LINK     = link32
LFLAGS   = -debug:minimal -debugtype:coff -machine:i386 -align:0x1000
IMPLIB   = lib32 -machine:i386
CVTRES   = cvtres -i386

DISPDLL_FLAGS = -dll -subsystem:native
DISPDLL_LIBS  = kernel32.lib $(DDKLIB)\winsrv.lib

LAYER_DEF = layer\layer31.def
LAYER_RES = layer\layer.rbj
RCFLAGS   = -DLY_NT31
!include layer.mak
