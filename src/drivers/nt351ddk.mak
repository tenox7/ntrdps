# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

DDKROOT  = C:\DDK
DDKLIB   = $(DDKROOT)\lib\i386\free

CC       = cl
CFLAGS   = -nologo -c -Zel -Zp8 -Gy -W3 -Gz -G4 -Oxs -Gf -QI6 -Oy \
           -DWINVER=0x030A -D_X86_=1 -Di386=1 -DSTD_CALL \
           -DCONDITION_HANDLING=1 -DWIN32_LEAN_AND_MEAN=1 -DNT_UP=1 \
           -DNT_INST=0 -DWIN32=100 -D_NT1X_=100 -DDBG=0 -DDEVL=1 -DFPO=1 \
           -I. -I$(DDKROOT)\inc

LINK     = link
LFLAGS   = -nologo -merge:_PAGE=PAGE -merge:_TEXT=.text -section:INIT,d \
           -opt:ref -release -incremental:no -fullbuild -force:multiple \
           -ignore:4001,4037,4039,4065,4070,4078,4087,4089 -pdb:none \
           -debug:notmapped,minimal -version:3.51 -osversion:3.51 \
           -merge:.rdata=.text
IMPLIB   = lib -nologo -machine:i386

DISPDLL_FLAGS = -dll -noentry -subsystem:native,3.51
DISPDLL_LIBS  = $(DDKLIB)\int64.lib kernel32.lib $(DDKLIB)\winsrv.lib

LAYER_DEF = layer\layer351.def
LAYER_RES = layer\layer.res
RCFLAGS   =
!include layer.mak
