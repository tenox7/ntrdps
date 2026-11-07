# ntrdps

An RDP server for the console of Windows NT 3.1, 3.5, 3.51 and 4.0 (x86).
It is for pre-TSE NT (before Terminal Server Edition): an RDP client connects to WinStation 0,
the console, so it is console only and not multi-user.

**No encryption and no login.** Anyone who can reach TCP port 3389 gets the console.
Use it only on a network you trust. Tested in QEMU only, not yet on real hardware.

## Install

1. Download `ntrdpset.exe` from [Releases](https://github.com/tenox7/ntrdps/releases).
2. Log on as an administrator and run it.
3. Click **Install**. Let it restart Windows NT.

It copies the files to `C:\NTRDPS`, adds the service `ntrdps` and the display driver,
and starts the service. **Remove** takes all of it away again.

If Windows NT does not start after the restart: choose "Last Known Good" at boot.
That undoes the install. Then run `ntrdpset.exe` again and click **Remove**.

In a batch file: `ntrdpset /install /q` (or `/remove`, `/status`). ERRORLEVEL: 0 done,
1 not an administrator, 2 failed, 3 nothing to remove.

## Connect

| Client | Command |
|---|---|
| rdesktop | `rdesktop -e HOST` |
| FreeRDP 3 | `xfreerdp3 /v:HOST /sec:rdp /cert:ignore /u:x /p:x` (or `sdl-freerdp`) |
| mstsc 5.0, 5.1 | `mstsc /v:HOST` (Ctrl+Alt+End for Ctrl+Alt+Del) |

mstsc 6.0 and later do not work.

## Build

With nmake, in a Command Prompt with the compiler's environment set:

| What | Compiler | Folder | Command |
|---|---|---|---|
| ntrdps for NT 3.1 | NT 3.1 SDK | `src` | `nmake -f nt31.mak` |
| ntrdps for NT 3.5, 3.51, 4.0 | Visual C++ 2.0 | `src` | `nmake -f nt35.mak` |
| ntrdps for NT 4.0 only | Visual C++ 4.2 | `src` | `nmake -f nt40.mak` |
| Driver for NT 3.1 | NT 3.1 SDK, NT 3.1 DDK | `src\drivers` | `nmake -f nt31ddk.mak` |
| Driver for NT 3.5, 3.51 | Visual C++ 2.0, NT 3.51 DDK | `src\drivers` | `nmake -f nt351ddk.mak` |
| Drivers for NT 4.0 | Visual C++ 4.2, NT 4.0 DDK | `src\drivers` | `nmake -f nt40ddk.mak` |
| Installer `ntrdpset.exe` | NT 3.1 SDK | `src` | `nmake -f setup.mak` |

The drivers look for the DDK in `C:\DDK`. Another folder: add `DDKROOT=D:\NTDDK` (for example).

The installer packs the files of the other builds. Copy them to `src\setup` first:

| Folder | Files |
|---|---|
| `nt31` | `ntrdps.exe`, `ntrdpshk.dll` (nt31.mak), `ntrdpsly.dll` (nt31ddk.mak) |
| `nt35` | `ntrdps.exe`, `ntrdpshk.dll` (nt35.mak), `ntrdpsly.dll` (nt351ddk.mak) |
| `nt40` | `ntrdpsmr.sys`, `ntrdpsmr.dll`, `ntrdpsm0.dll` (nt40ddk.mak) |

On macOS or Linux, `build.sh` runs the same makefiles in Docker
(images `tenox7/nt31sdk`, `tenox7/nt35sdk`, `tenox7/nt40sdk`). `DDK` is a folder with
`nt31/DDK`, `nt351/DDK` and `nt40/ddk`. The binaries go to `out/`, the installer to `out/setup/`.

```
DDK=/path/to/ddks ./build.sh
```

## License

MIT
