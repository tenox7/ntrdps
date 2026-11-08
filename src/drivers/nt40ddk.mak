# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

DDKROOT  = C:\DDK
DDKLIB   = $(DDKROOT)\lib\i386\free

CC       = cl
CFLAGS   = -nologo -c -Zel -Zp8 -Gy -cbstring -W3 -Gz -QIfdiv- -QIf \
           -Gi- -Gm- -GX- -GR- -GF -Oxs -Oy \
           -D_X86_=1 -Di386=1 -DSTD_CALL -DCONDITION_HANDLING=1 \
           -DNT_UP=1 -DNT_INST=0 -DWIN32=100 -D_NT1X_=100 -DWINNT=1 \
           -D_WIN32_WINNT=0x0400 -DWIN32_LEAN_AND_MEAN=1 -DDEVL=1 \
           -DFPO=1 -DNDEBUG -DDBG=0 \
           -I. -I$(DDKROOT)\inc -FI$(DDKROOT)\inc\warning.h
CFLAGS_MINIPORT = $(CFLAGS) -I$(DDKROOT)\src\video\inc

LINK     = link
LFLAGS   = -nologo -machine:i386 -merge:_PAGE=PAGE -merge:_TEXT=.text \
           -section:INIT,d -opt:ref -incremental:no -force:multiple \
           -release -fullbuild \
           -ignore:4001,4037,4039,4065,4070,4078,4087,4089,4096 \
           -nodefaultlib -debug:notmapped,minimal -debugtype:coff \
           -version:4.00 -osversion:4.00 -pdb:none -merge:.rdata=.text \
           -optidata -driver -align:0x20
IMPLIB   = lib -nologo -machine:i386
RC       = rc

GDIDRV_FLAGS = -dll -subsystem:native,4.00 -base:0x10000 \
               -entry:DrvEnableDriver@12
GDIDRV_LIBS  = $(DDKLIB)\win32k.lib $(DDKLIB)\int64.lib

MINIPORT_FLAGS = -subsystem:native,4.00 -base:0x10000 \
                 -entry:DriverEntry@8
MINIPORT_LIBS  = $(DDKLIB)\videoprt.lib $(DDKLIB)\int64.lib

!include mirror.mak
