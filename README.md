# NTRDPS

A Remote Desktop (RDP) Server for Windows NT 3.1, 3.5, 3.51 and 4.0, pre-TSE Hydra and pre-WinFrame. RDP client connects to `WinSta0`, or the local console. No WTS, no MultiWin.

Yes you can Remote Desktop to NT 3.1...

## Install

Run `ntrdpset-<CPU>.exe`.

## History and prior art

While services and background processes on Windows NT could always assume role / run as specific user, the Windows desktop was single user and the local console only.

Applications like Symantec pcANYWHERE, Dameware Mini Remote Control or VNC allowed remotely controlling the local desktop by use of a mirror driver or GDI polling.

Citrix licensed source code of Windows to develop MultiWin feature. In September 1995 they shipped a product called WinFrame, which introduced concept of WinStation and remote connectivity with ICA protocol.

Around 1998 Microsoft released Windows NT 4.0 Terminal Server Edition (TSE) aka "Hydra". It came with a new protocol named Remote Desktop Protocol (RDP), which unlike ICA, was Microsoft owned. Citrix continued shipping ICA protocol as an TSE addon called Metaframe. Later TSE was renamed to WTS (Windows Terminal Services).

Interestingly RDP protocol was developed at Microsoft out of [NetMeeting](https://en.wikipedia.org/wiki/NetMeeting) and ITU T.120/128. There is [a video](https://www.youtube.com/watch?v=qhay6VryyvE) about it.

NTRDPS is based on the same protocol from NetMeeting but connects only to the console like pcANYWHERE/DMRC/VNC.

## FAQ

Q: Which RDP version is supported
A: 4.0 / 5.x

Q: Driver or GDI Polling
A: The driver is faster. On NT 3.x, if it can't follow the video card, ntrdps falls back to GDI Polling. Setup shows which one is active.

## License

MIT
