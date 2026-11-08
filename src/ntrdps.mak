# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

INC   = -Icore -Irdp -Icapture -Iservice -Idrivers\common
OBJS  = service\main.obj service\log.obj service\config.obj core\net.obj \
        service\mrinst.obj \
        rdp\stream.obj rdp\frame.obj rdp\session.obj $(PROTO) $(CORE) \
        core\desktop.obj core\input.obj core\keymap.obj core\sas.obj \
        core\prompt.obj capture\poll.obj capture\cursor.obj capture\mirror.obj \
        capture\mrshape.obj capture\hookdll.obj capture\reader.obj
TESTS = tests\t_stream.exe tests\t_log.exe tests\t_frame.exe tests\t_proto.exe \
        tests\t_rle.exe tests\t_keymap.exe tests\t_config.exe \
        tests\t_reduce.exe tests\t_record.exe tests\t_mrshape.exe \
        tests\t_orders.exe tests\t_draw.exe tests\t_mppc.exe

CORE  = core\core.obj core\draw.obj core\gstore.obj core\bstore.obj \
        capture\testpat.obj capture\palette.obj

PROTO = rdp\rdp.obj rdp\x224.obj rdp\mcs.obj rdp\gcc.obj rdp\sec.obj \
        rdp\lic.obj rdp\caps.obj rdp\share.obj rdp\update.obj \
        rdp\inputpdu.obj rdp\rle.obj rdp\pointer.obj rdp\reduce.obj \
        rdp\orders.obj rdp\glyphs.obj rdp\bitmaps.obj rdp\mppc.obj

all: ntrdps.exe ntrdpshk.dll

tests: $(TESTS)

ddtest: tests\ddtest.exe

{core}.c{core}.obj:
	$(CC) $(CFLAGS_MT) $(INC) -Fo$@ $<
{rdp}.c{rdp}.obj:
	$(CC) $(CFLAGS_MT) $(INC) -Fo$@ $<
{capture}.c{capture}.obj:
	$(CC) $(CFLAGS_MT) $(INC) -Fo$@ $<
{service}.c{service}.obj:
	$(CC) $(CFLAGS_MT) $(INC) -Fo$@ $<
{tests}.c{tests}.obj:
	$(CC) $(CFLAGS_MT) $(INC) -Fo$@ $<
{hook}.c{hook}.obj:
	$(CC) $(CFLAGS_DLL) $(INC) -Fo$@ $<
{drivers\common}.c{drivers\common}.obj:
	$(CC) $(CFLAGS_MT) $(INC) -Fo$@ $<

ntrdps.exe: $(OBJS)
	$(LINK) $(LFLAGS) $(EXE_CON) -out:ntrdps.exe $(OBJS) $(LIBS_MT) $(WINLIBS)

ntrdpshk.exp: hook\hook.obj hook\ntrdpshk.def
	$(IMPLIB) -def:hook\ntrdpshk.def hook\hook.obj -out:ntrdpshk.lib

ntrdpshk.dll: hook\hook.obj ntrdpshk.exp
	$(LINK) $(LFLAGS) $(DLL_FLAGS) -base:0x6E200000 -section:.shared,rws \
	    -out:ntrdpshk.dll ntrdpshk.exp hook\hook.obj user32.lib kernel32.lib \
	    $(DLL_RTLIB)

tests\t_stream.exe: tests\t_stream.obj rdp\stream.obj
	$(LINK) $(LFLAGS) $(EXE_CON) -out:$@ tests\t_stream.obj rdp\stream.obj $(LIBS_MT)

tests\t_log.exe: tests\t_log.obj service\log.obj
	$(LINK) $(LFLAGS) $(EXE_CON) -out:$@ tests\t_log.obj service\log.obj $(LIBS_MT)

tests\t_frame.exe: tests\t_frame.obj rdp\frame.obj
	$(LINK) $(LFLAGS) $(EXE_CON) -out:$@ tests\t_frame.obj rdp\frame.obj $(LIBS_MT)

tests\t_rle.exe: tests\t_rle.obj tests\rledec.obj rdp\rle.obj
	$(LINK) $(LFLAGS) $(EXE_CON) -out:$@ tests\t_rle.obj tests\rledec.obj \
	    rdp\rle.obj $(LIBS_MT)

tests\t_keymap.exe: tests\t_keymap.obj core\keymap.obj service\log.obj
	$(LINK) $(LFLAGS) $(EXE_CON) -out:$@ tests\t_keymap.obj core\keymap.obj \
	    service\log.obj $(LIBS_MT)

tests\t_reduce.exe: tests\t_reduce.obj rdp\reduce.obj
	$(LINK) $(LFLAGS) $(EXE_CON) -out:$@ tests\t_reduce.obj rdp\reduce.obj \
	    $(LIBS_MT)

tests\t_record.exe: tests\t_record.obj drivers\common\record.obj
	$(LINK) $(LFLAGS) $(EXE_CON) -out:$@ tests\t_record.obj \
	    drivers\common\record.obj $(LIBS_MT)

tests\t_orders.exe: tests\t_orders.obj rdp\orders.obj core\draw.obj rdp\reduce.obj
	$(LINK) $(LFLAGS) $(EXE_CON) -out:$@ tests\t_orders.obj rdp\orders.obj \
	    core\draw.obj rdp\reduce.obj $(LIBS_MT)

tests\t_draw.exe: tests\t_draw.obj core\draw.obj core\gstore.obj \
                  core\bstore.obj
	$(LINK) $(LFLAGS) $(EXE_CON) -out:$@ tests\t_draw.obj core\draw.obj \
	    core\gstore.obj core\bstore.obj $(LIBS_MT)

tests\t_mrshape.exe: tests\t_mrshape.obj capture\mrshape.obj
	$(LINK) $(LFLAGS) $(EXE_CON) -out:$@ tests\t_mrshape.obj \
	    capture\mrshape.obj $(LIBS_MT)

tests\ddtest.exe: tests\ddtest.obj
	$(LINK) $(LFLAGS) $(EXE_CON) -out:$@ tests\ddtest.obj $(LIBS_MT) \
	    ddraw.lib

tests\t_mppc.exe: tests\t_mppc.obj rdp\mppc.obj service\log.obj
	$(LINK) $(LFLAGS) $(EXE_CON) -out:$@ tests\t_mppc.obj rdp\mppc.obj \
	    service\log.obj $(LIBS_MT)

tests\t_config.exe: tests\t_config.obj service\config.obj service\log.obj
	$(LINK) $(LFLAGS) $(EXE_CON) -out:$@ tests\t_config.obj \
	    service\config.obj service\log.obj $(LIBS_MT) advapi32.lib

T_PROTO = tests\t_proto.obj tests\tsess.obj tests\tinput.obj tests\rledec.obj \
          $(PROTO) $(CORE) rdp\frame.obj rdp\stream.obj service\log.obj
tests\t_proto.exe: $(T_PROTO)
	$(LINK) $(LFLAGS) $(EXE_CON) -out:$@ $(T_PROTO) $(LIBS_MT)
