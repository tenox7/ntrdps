# ntrdps

An RDP server for the console of Windows NT 3.1, 3.5, 3.51 and 4.0,
on Intel x86, MIPS, Alpha and PowerPC.
It is for pre-TSE NT (before Terminal Server Edition): an RDP client connects to WinStation 0,
the console, so it is console only and not multi-user.

**Login.** A client must log on before it gets the console (on by default; since release 0.6,
x86 only so far - see Install):

| NT version | Who may log on |
|---|---|
| NT 3.51, 4.0 | An account of the NT machine that is in the group Administrators. Windows checks the password. |
| NT 3.1, 3.5 | The ntrdps account: one user name and password, set in the installer. These NT versions cannot check a Windows password for a program. |

The client can send the user name and password itself (FreeRDP `/u: /p:`, rdesktop `-u -p`).
If it sends none, or a wrong one, the client gets the logon dialog of Windows itself, on every
NT version. 3 wrong tries or 60 s: the connection ends.

Encryption is optional: a client that asks for TLS gets an encrypted connection; a client that
asks for plain RDP gets plain text, **the password too**. This works on every CPU - x86, MIPS,
Alpha and PowerPC. To allow only TLS clients to log on: set the REG_DWORD `LoginPlain` = 0 in
`HKEY_LOCAL_MACHINE\SOFTWARE\ntrdps`.

Every copy of ntrdps has the same built-in TLS certificate, and its private key is public (it is
in `src/tls`). So TLS stops someone who only listens. It does not stop someone who can get
between you and the server: that one could read the password too.

Use ntrdps only on a network you trust. Tested in QEMU only, not yet on real hardware.

## Install

1. Download the installer for your CPU from [Releases](https://github.com/tenox7/ntrdps/releases).
   It finds the NT version itself.
2. Log on as an administrator and run it.
3. Leave **Ask for a user name and password** ticked. Click **Install**. On NT 3.1 and 3.5 it
   then asks for the user name and password of the ntrdps account. Let it restart Windows NT.

| CPU | Installer | Release | Tested on |
|---|---|---|---|
| Intel x86 | `ntrdpset-i386.exe` | 0.6 | NT 3.1, 3.5, 3.51, 4.0 |
| MIPS | `ntrdpset-mips.exe` | 0.5, **no login** | NT 3.1, 3.51, 4.0 |
| Alpha | `ntrdpset-alpha.exe` | 0.5, **no login** | NT 3.1, 3.51, 4.0 |
| PowerPC | `ntrdpset-ppc.exe` | 0.5, **no login** | NT 3.51, 4.0 |

MIPS, Alpha and PowerPC have no 0.6 installer yet. Release 0.5 has no login: anyone who can
reach TCP port 3389 gets the console.

It copies the files to `C:\NTRDPS`, adds the service `ntrdps` and the display driver,
and starts the service. **Remove** takes all of it away again.

If Windows NT does not start after the restart: choose "Last Known Good" at boot.
That undoes the install. Then run the installer again and click **Remove**.

In a batch file: `ntrdpset /install /q` (or `/remove`, `/status`). ERRORLEVEL: 0 done,
1 not an administrator, 2 failed, 3 nothing to remove. More options for `/install`:

| Option | What |
|---|---|
| `/user:NAME /password:PASSWORD` | NT 3.1 and 3.5: the ntrdps account. Needed there, unless an account is set already. |
| `/nologin` | No login: anyone who can reach TCP port 3389 gets the console. |
| `/poll` | No display driver: read the screen instead. |

An update from 0.5.1 or earlier turns the login on.

## Connect

| Client | Command |
|---|---|
| rdesktop 1.9 (asks for TLS) | `rdesktop -e -u NAME -p PASSWORD HOST` |
| FreeRDP 3, TLS | `xfreerdp3 /v:HOST /sec:tls /cert:ignore /u:NAME /p:PASSWORD` (or `sdl-freerdp`) |
| FreeRDP 3, plain | `xfreerdp3 /v:HOST /sec:rdp /cert:ignore /u:NAME /p:PASSWORD` |
| mstsc 5.0, 5.1 (plain) | `mstsc /v:HOST`, then log on in the logon dialog (Ctrl+Alt+End for Ctrl+Alt+Del) |

TLS works on every build - x86, MIPS, Alpha and PowerPC. Current Microsoft clients (mstsc 6.0
and later, Windows App) use TLS. They were used with ntrdps (mstsc of Windows 10, Windows App on
a Mac), but they are not in our tests yet.

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

MIPS, Alpha and PowerPC: the same in a Command Prompt on that machine. Add `CPU=MIPS`,
`CPU=ALPHA` or `CPU=PPC` to each command.

| What | Compiler | Folder | Command |
|---|---|---|---|
| ntrdps for NT 3.1 | NT 3.1 SDK | `src` | `nmake -f risc31.mak` |
| ntrdps for NT 3.5, 3.51, 4.0 | Visual C++ 4.1 | `src` | `nmake -f risc35.mak` |
| Driver for NT 3.1 | NT 3.1 SDK, NT 3.1 DDK | `src\drivers` | `nmake -f risc31d.mak` |
| Driver for NT 3.5, 3.51 | Visual C++ 4.1, NT 3.51 DDK | `src\drivers` | `nmake -f risc351d.mak` |
| Drivers for NT 4.0 | Visual C++ 4.1, NT 4.0 SDK and DDK | `src\drivers` | `nmake -f risc40d.mak` |
| Installer, MIPS and Alpha | NT 3.1 SDK | `src` | `nmake -f rsetup31.mak` |
| Installer, PowerPC | Visual C++ 4.1 | `src` | `nmake -f rsetup35.mak` |

There is no NT 3.1 for PowerPC, so its installer has no `nt31` files.

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
