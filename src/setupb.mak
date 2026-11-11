# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.

!IF "$(CPU)" == "PPC"
SETUP_DEFS = -DNO_NT31
FILES31    =
!ELSE
SETUP_DEFS =
FILES31    = setup\nt31\ntrdps.exe setup\nt31\ntrdpshk.dll setup\nt31\ntrdpsly.dll
!ENDIF

FILES = $(FILES31) \
        setup\nt35\ntrdps.exe setup\nt35\ntrdpshk.dll setup\nt35\ntrdpsly.dll \
        setup\nt40\ntrdpsmr.sys setup\nt40\ntrdpsmr.dll setup\nt40\ntrdpsm0.dll

!IFDEF TLS
SETUP_RCDEFS = -DNTRDPS_LOGIN
SETUP_INC    = -Idrivers\common
SETUP_LIBS   = gdi32.lib
!ENDIF

!IFDEF CVTRES
SETUP_RES = setup\setup.rbj
!ELSE
SETUP_RES = setup\setup.res
!ENDIF

all: ntrdpset.exe

setup\setup.obj: setup\setup.c setup\setup.h service\version.h
	$(CC) $(CFLAGS_MT) $(SETUP_DEFS) -Iservice $(SETUP_INC) -Fosetup\setup.obj setup\setup.c

setup\setup.res: setup\setup.rc setup\setup.h $(FILES)
	rc -r $(SETUP_DEFS) $(SETUP_RCDEFS) -fo setup\setup.res setup\setup.rc

setup\setup.rbj: setup\setup.res
	$(CVTRES) setup\setup.res -o setup\setup.rbj

ntrdpset.exe: setup\setup.obj $(SETUP_RES)
	$(LINK) $(LFLAGS) $(EXE_GUI) \
	    -out:ntrdpset.exe setup\setup.obj $(SETUP_RES) $(LIBS_MT) \
	    user32.lib advapi32.lib $(SETUP_LIBS)
