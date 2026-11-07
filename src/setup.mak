# ntrdps - RDP console server for Windows NT
# Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE.


CC        = cl386
CFLAGS    = -nologo -c -W3 -Ox -D_X86_=1 -DWIN32
CFLAGS_MT = $(CFLAGS) -D_MT
CFLAGS_DLL = $(CFLAGS) -Zl

LINK      = link32
LFLAGS    = -align:0x1000
IMPLIB    = lib32 -machine:i386

EXE_CON   = -subsystem:console -entry:mainCRTStartup
LIBS_MT   = libcmt.lib kernel32.lib

DLL_FLAGS = -dll -subsystem:windows -entry:DllMain@12

WINLIBS   = user32.lib gdi32.lib advapi32.lib wsock32.lib


FILES = setup\nt31\ntrdps.exe setup\nt31\ntrdpshk.dll setup\nt31\ntrdpsly.dll \
        setup\nt35\ntrdps.exe setup\nt35\ntrdpshk.dll setup\nt35\ntrdpsly.dll \
        setup\nt40\ntrdpsmr.sys setup\nt40\ntrdpsmr.dll setup\nt40\ntrdpsm0.dll

all: ntrdpset.exe

setup\setup.obj: setup\setup.c setup\setup.h service\version.h
	$(CC) $(CFLAGS_MT) -Iservice -Fosetup\setup.obj setup\setup.c

setup\setup.res: setup\setup.rc setup\setup.h $(FILES)
	rc -r -fo setup\setup.res setup\setup.rc

setup\setup.rbj: setup\setup.res
	cvtres -i386 setup\setup.res -o setup\setup.rbj

ntrdpset.exe: setup\setup.obj setup\setup.rbj
	$(LINK) $(LFLAGS) -subsystem:windows -entry:WinMainCRTStartup \
	    -out:ntrdpset.exe setup\setup.obj setup\setup.rbj $(LIBS_MT) \
	    user32.lib advapi32.lib
