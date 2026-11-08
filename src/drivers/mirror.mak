# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

INC  = -Imirror -Icommon
OBJS = mirror\mirror.obj common\shadow.obj common\record.obj
OBJS0 = mirror\mirror0.obj common\shadow.obj common\record.obj

all: ntrdpsmr.dll ntrdpsm0.dll ntrdpsmr.sys

{mirror}.c{mirror}.obj:
	$(CC) $(CFLAGS) $(INC) -Fo$@ $<
{common}.c{common}.obj:
	$(CC) $(CFLAGS) $(INC) -Fo$@ $<

mirror\miniport.obj: mirror\miniport.c
	$(CC) $(CFLAGS_MINIPORT) $(INC) -Fo$@ mirror\miniport.c

mirror\mirror0.obj: mirror\mirror.c
	$(CC) $(CFLAGS) -DMR_RTM $(INC) -Fo$@ mirror\mirror.c

w32ksp3.lib: mirror\w32ksp3.def mirror\w32kstub.obj
	$(IMPLIB) -def:mirror\w32ksp3.def mirror\w32kstub.obj -out:w32ksp3.lib

mirror\mirror.res: mirror\mirror.rc
	$(RC) -r -fo mirror\mirror.res mirror\mirror.rc

ntrdpsmr.dll: $(OBJS) mirror\mirror.res w32ksp3.lib
	$(LINK) $(LFLAGS) $(GDIDRV_FLAGS) -out:ntrdpsmr.dll $(OBJS) mirror\mirror.res w32ksp3.lib $(GDIDRV_LIBS)

ntrdpsm0.dll: $(OBJS0) mirror\mirror.res
	$(LINK) $(LFLAGS) $(GDIDRV_FLAGS) -out:ntrdpsm0.dll $(OBJS0) mirror\mirror.res $(GDIDRV_LIBS)

ntrdpsmr.sys: mirror\miniport.obj
	$(LINK) $(LFLAGS) $(MINIPORT_FLAGS) -out:ntrdpsmr.sys mirror\miniport.obj $(MINIPORT_LIBS)
