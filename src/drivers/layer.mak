# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

INC  = -Ilayer -Icommon
OBJS = layer\layer.obj common\shadow.obj common\record.obj

all: ntrdpsly.dll

{layer}.c{layer}.obj:
	$(CC) $(CFLAGS) -DMR_LAYER $(INC) -Fo$@ $<
{common}.c{common}.obj:
	$(CC) $(CFLAGS) -DMR_LAYER $(INC) -Fo$@ $<

ntrdpsly.exp: $(OBJS) $(LAYER_DEF)
	$(IMPLIB) -def:$(LAYER_DEF) $(OBJS) -out:ntrdpsly.lib

layer\layer.res: layer\layer.rc
	rc -r $(RCFLAGS) -fo layer\layer.res layer\layer.rc

layer\layer.rbj: layer\layer.res
	cvtres -i386 layer\layer.res -o layer\layer.rbj

ntrdpsly.dll: $(OBJS) ntrdpsly.exp $(LAYER_RES)
	$(LINK) $(LFLAGS) $(DISPDLL_FLAGS) -base:0x62600000 -out:ntrdpsly.dll ntrdpsly.exp $(OBJS) $(LAYER_RES) $(DISPDLL_LIBS)
