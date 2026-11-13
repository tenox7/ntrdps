# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

DDKROOT  = E:\NTDDK
DDKBIN   = $(DDKROOT)\bin\AXP64
DDKLIB   = $(DDKROOT)\libfre\axp64

CC       = $(DDKBIN)\cl
CFLAGS   = -nologo -c -Ap64 -Wp64 -Zel -Zp8 -Gy -W3 -Ox -GX- -GR- -GF -Gt0 \
           -D_WIN64=1 -D_AXP64_=1 -D_M_AXP64 -D_M_ALPHA64 -DALPHA=1 -D_ALPHA_=1 \
           -DNO_EXT_KEYS -DCONDITION_HANDLING=1 -DNT_UP=1 -DNT_INST=0 \
           -DWIN32=100 -D_NT1X_=100 -DWINNT=1 -D_WIN32_WINNT=0x0500 \
           -DWIN32_LEAN_AND_MEAN=1 -DDEVL=1 -DFPO=1 -DNDEBUG -DDBG=0 \
           -D__stdcall= -D__cdecl= \
           -I. -I$(DDKROOT)\inc -FI$(DDKROOT)\inc\warning.h
CFLAGS_MINIPORT = $(CFLAGS) -I$(DDKROOT)\inc\ddk -I$(DDKROOT)\src\video\inc

LINK     = $(DDKBIN)\link
LFLAGS   = -nologo -machine:alpha64 -opt:ref -incremental:no \
           -force:multiple -release -nodefaultlib \
           -version:4.00 -osversion:4.00 -pdb:none -driver
IMPLIB   = $(DDKBIN)\lib -nologo -machine:alpha64
RC       = rc

GDIDRV_FLAGS   = -dll -subsystem:native,4.00 -base:0x10000 \
                 -entry:DrvEnableDriver
GDIDRV_LIBS    = win32k.lib $(DDKLIB)\libcntpr.lib
MINIPORT_FLAGS = -subsystem:native,4.00 -base:0x10000 -entry:DriverEntry
MINIPORT_LIBS  = videoprt.lib $(DDKLIB)\libcntpr.lib


INC   = -Imirror -Icommon
OBJS5 = mirror\mirror5.obj common\shadow.obj common\record.obj

all: ntrdpsm5.dll ntrdpsmr.sys

{common}.c{common}.obj:
	$(CC) $(CFLAGS) $(INC) -Fo$@ $<

mirror\mirror5.obj: mirror\mirror.c
	$(CC) $(CFLAGS) -DMR_NT5 $(INC) -Fo$@ mirror\mirror.c

mirror\miniport.obj: mirror\miniport.c
	$(CC) $(CFLAGS_MINIPORT) $(INC) -Fo$@ mirror\miniport.c

win32k.lib: mirror\w32k64.def
	$(IMPLIB) -def:mirror\w32k64.def -out:win32k.lib

videoprt.lib: mirror\vprt64.def
	$(IMPLIB) -def:mirror\vprt64.def -out:videoprt.lib

mirror\mirror.res: mirror\mirror.rc
	$(RC) -r -fo mirror\mirror.res mirror\mirror.rc

ntrdpsm5.dll: $(OBJS5) mirror\mirror.res win32k.lib
	$(LINK) $(LFLAGS) $(GDIDRV_FLAGS) -out:ntrdpsm5.dll $(OBJS5) mirror\mirror.res $(GDIDRV_LIBS)

ntrdpsmr.sys: mirror\miniport.obj videoprt.lib
	$(LINK) $(LFLAGS) $(MINIPORT_FLAGS) -out:ntrdpsmr.sys mirror\miniport.obj $(MINIPORT_LIBS)
