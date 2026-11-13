# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

DDKROOT  = E:\NTDDK
DDKLIB   = $(DDKROOT)\libfre\alpha

CC       = cl
CFLAGS   = -nologo -c -Zel -Zp8 -Gy -W3 -Ox -GX- -GR- -GF -Gt0 \
           -DALPHA=1 -D_ALPHA_=1 \
           -DNO_EXT_KEYS -DCONDITION_HANDLING=1 -DNT_UP=1 -DNT_INST=0 \
           -DWIN32=100 -D_NT1X_=100 -DWINNT=1 -D_WIN32_WINNT=0x0500 \
           -DWIN32_LEAN_AND_MEAN=1 -DDEVL=1 -DFPO=1 -DNDEBUG -DDBG=0 \
           -D__stdcall= -D__cdecl= \
           -I. -I$(DDKROOT)\inc -FI$(DDKROOT)\inc\warning.h

LINK     = link
LFLAGS   = -nologo -machine:alpha -opt:ref -incremental:no \
           -force:multiple -release -nodefaultlib \
           -debug:notmapped,minimal -debugtype:coff \
           -version:4.00 -osversion:4.00 -pdb:none -driver
RC       = rc

GDIDRV_FLAGS = -dll -subsystem:native,4.00 -base:0x10000 \
               -entry:DrvEnableDriver
GDIDRV_LIBS  = $(DDKLIB)\win32k.lib $(DDKLIB)\libcntpr.lib


INC   = -Imirror -Icommon
OBJS5 = mirror\mirror5.obj common\shadow.obj common\record.obj

all: ntrdpsm5.dll

{common}.c{common}.obj:
	$(CC) $(CFLAGS) $(INC) -Fo$@ $<

mirror\mirror5.obj: mirror\mirror.c
	$(CC) $(CFLAGS) -DMR_NT5 $(INC) -Fo$@ mirror\mirror.c

mirror\mirror.res: mirror\mirror.rc
	$(RC) -r -fo mirror\mirror.res mirror\mirror.rc

ntrdpsm5.dll: $(OBJS5) mirror\mirror.res
	$(LINK) $(LFLAGS) $(GDIDRV_FLAGS) -out:ntrdpsm5.dll $(OBJS5) mirror\mirror.res $(GDIDRV_LIBS)
