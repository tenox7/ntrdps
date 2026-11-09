/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <winsock.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "net.h"
#ifdef NTRDPS_TLS
#include "tls.h"
#endif

#define LISTEN_POLL_MS 250
#define BIND_RETRY_MS  5000
#define STOP_WAIT_MS   10000

#define PROCESS_VM_COUNTERS   3
#define PROCESS_HANDLE_COUNT  20
#define HANDLE_PROBE_END      0x4000

typedef LONG (WINAPI *QUERY_PROCESS)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef BOOL (WINAPI *GET_HANDLE_INFO)(HANDLE, LPDWORD);

typedef struct {
    ULONG peak_virtual;
    ULONG virtual_size;
    ULONG page_faults;
    ULONG peak_working_set;
    ULONG working_set;
    ULONG pool_quota[4];
    ULONG committed;
    ULONG peak_committed;
} VM_NUMBERS;

typedef struct {
    SOCKET         sock;
    char           peer[NET_PEER_MAX];
    volatile LONG  stop;
    NET_SESSION_FN fn;
    HANDLE         thread;
} SLOT;

static SLOT g_slots[NET_SLOTS];

const char *net_error_text(int err)
{
    switch (err) {
    case WSAECONNRESET:   return " (connection reset)";
    case WSAECONNABORTED: return " (connection aborted)";
    case WSAETIMEDOUT:    return " (timed out)";
    case WSAEADDRINUSE:   return " (port in use)";
    case WSAENETDOWN:     return " (network down)";
    case WSAENOBUFS:      return " (no buffer space)";
    }
    return "";
}

static DWORD WINAPI slot_thread(LPVOID arg)
{
    SLOT *slot = (SLOT *) arg;

    slot->fn(slot->sock, slot->peer, &slot->stop);
    return 0;
}

static void log_process(int open)
{
    static QUERY_PROCESS query;
    static GET_HANDLE_INFO handle_info;
    static int looked;
    HMODULE dll;
    VM_NUMBERS vm;
    ULONG n, got;
    DWORD h, flags;
    char mem[100], handles[40];

    if (!looked) {
        looked = 1;
        dll = GetModuleHandleA("NTDLL.DLL");
        if (dll != NULL) {
            query = (QUERY_PROCESS)
                GetProcAddress(dll, "NtQueryInformationProcess");
        }
        dll = GetModuleHandleA("KERNEL32.DLL");
        if (dll != NULL) {
            handle_info = (GET_HANDLE_INFO) GetProcAddress(dll,
                                                    "GetHandleInformation");
        }
    }
    strcpy(mem, "memory not known");
    got = 0;
    if (query != NULL &&
        query(GetCurrentProcess(), PROCESS_VM_COUNTERS, &vm, sizeof(vm),
              &got) >= 0 && got == sizeof(vm)) {
        sprintf(mem, "memory %lu KB (peak %lu KB), working set %lu KB",
                vm.committed / 1024, vm.peak_committed / 1024,
                vm.working_set / 1024);
    }
    strcpy(handles, "handles not known");
    if (query != NULL &&
        query(GetCurrentProcess(), PROCESS_HANDLE_COUNT, &n, sizeof(n),
              &got) >= 0) {
        sprintf(handles, "%lu handles", n);
    } else if (handle_info != NULL) {
        n = 0;
        for (h = 4; h < HANDLE_PROBE_END; h += 4) {
            if (handle_info((HANDLE) h, &flags)) {
                n++;
            }
        }
        sprintf(handles, "%lu handles (counted)", n);
    }
    log_msg(LOG_INFO, "process: %s, %s, %d connection%s open", mem, handles,
            open, open == 1 ? "" : "s");
}

static void slots_reap(void)
{
    int i, reaped = 0, open = 0;

    for (i = 0; i < NET_SLOTS; i++) {
        if (g_slots[i].thread != NULL &&
            WaitForSingleObject(g_slots[i].thread, 0) == WAIT_OBJECT_0) {
            CloseHandle(g_slots[i].thread);
            g_slots[i].thread = NULL;
            reaped++;
        }
        if (g_slots[i].thread != NULL) {
            open++;
        }
    }
    if (reaped > 0) {
        log_process(open);
    }
}

static void slots_stop(void)
{
    DWORD t0 = GetTickCount(), spent;
    int i;

    for (i = 0; i < NET_SLOTS; i++) {
        g_slots[i].stop = NET_STOP_SERVICE;
    }
    for (i = 0; i < NET_SLOTS; i++) {
        if (g_slots[i].thread == NULL) {
            continue;
        }
        spent = GetTickCount() - t0;
        if (WaitForSingleObject(g_slots[i].thread,
                                spent < STOP_WAIT_MS ? STOP_WAIT_MS - spent
                                                     : 0) != WAIT_OBJECT_0) {
            log_msg(LOG_ERROR, "session %s: its thread did not end within "
                    "%d s", g_slots[i].peer, STOP_WAIT_MS / 1000);
            continue;
        }
        CloseHandle(g_slots[i].thread);
        g_slots[i].thread = NULL;
    }
}

static int slot_start(SLOT *slot, SOCKET sock, const char *peer,
                      NET_SESSION_FN fn)
{
    DWORD tid;

    slot->sock = sock;
    strcpy(slot->peer, peer);
    slot->stop = 0;
    slot->fn = fn;
    slot->thread = CreateThread(NULL, 0, slot_thread, slot, 0, &tid);
    if (slot->thread == NULL) {
        log_msg(LOG_ERROR, "session %s: CreateThread failed, error %lu",
                peer, (unsigned long) GetLastError());
        return 0;
    }
    return 1;
}

static SOCKET open_listener(unsigned int port, int *err)
{
    SOCKET ls;
    struct sockaddr_in a;

    ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == INVALID_SOCKET) {
        *err = WSAGetLastError();
        return INVALID_SOCKET;
    }
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((u_short) port);
    if (bind(ls, (struct sockaddr *) &a, sizeof(a)) == SOCKET_ERROR ||
        listen(ls, SOMAXCONN) == SOCKET_ERROR) {
        *err = WSAGetLastError();
        closesocket(ls);
        return INVALID_SOCKET;
    }
    return ls;
}

static void accept_one(SOCKET ls, NET_SESSION_FN fn)
{
    struct sockaddr_in a;
    char peer[NET_PEER_MAX];
    SOCKET cs;
    int alen = sizeof(a);
    int err, i;

    cs = accept(ls, (struct sockaddr *) &a, &alen);
    if (cs == INVALID_SOCKET) {
        err = WSAGetLastError();
        log_msg(LOG_WARN, "accept failed, error %d%s", err,
                net_error_text(err));
        return;
    }
    sprintf(peer, "%.15s:%u", inet_ntoa(a.sin_addr),
            (unsigned) ntohs(a.sin_port));
    log_msg(LOG_INFO, "accepted %s", peer);
    slots_reap();
    for (i = 0; i < NET_SLOTS && g_slots[i].thread != NULL; i++) {
    }
    if (i == NET_SLOTS) {
        log_msg(LOG_WARN, "%s closed at once: %d connections are open",
                peer, NET_SLOTS);
        closesocket(cs);
        return;
    }
    if (!slot_start(&g_slots[i], cs, peer, fn)) {
        closesocket(cs);
    }
}

int net_serve(unsigned int port, HANDLE stop_event, NET_SESSION_FN fn)
{
    WSADATA wsa;
    SOCKET ls;
    fd_set rd;
    struct timeval tv;
    int n, err, warned = 0;

    if (WSAStartup(MAKEWORD(1, 1), &wsa) != 0) {
        log_msg(LOG_ERROR, "WSAStartup 1.1 failed: no TCP/IP?");
        return 1;
    }
    log_msg(LOG_INFO, "Winsock %u.%u: %.60s",
            (unsigned) LOBYTE(wsa.wVersion), (unsigned) HIBYTE(wsa.wVersion),
            wsa.szDescription);
#ifdef NTRDPS_TLS
    tls_startup();
    log_msg(LOG_INFO, "TLS available (experiment M12): a client may ask for "
            "PROTOCOL_SSL");
#endif

    for (;;) {
        ls = open_listener(port, &err);
        if (ls != INVALID_SOCKET) {
            break;
        }
        if (!warned) {
            log_msg(LOG_WARN, "cannot listen on TCP port %u, error %d%s; "
                    "trying again every %d s", port, err, net_error_text(err),
                    BIND_RETRY_MS / 1000);
            warned = 1;
        }
        if (WaitForSingleObject(stop_event, BIND_RETRY_MS) != WAIT_TIMEOUT) {
            WSACleanup();
            return 0;
        }
    }
    log_msg(LOG_INFO, "listening on TCP port %u", port);
    log_process(0);

    while (WaitForSingleObject(stop_event, 0) == WAIT_TIMEOUT) {
        slots_reap();
        FD_ZERO(&rd);
        FD_SET(ls, &rd);
        tv.tv_sec = 0;
        tv.tv_usec = LISTEN_POLL_MS * 1000L;
        n = select(0, &rd, NULL, NULL, &tv);
        if (n == SOCKET_ERROR) {
            err = WSAGetLastError();
            log_msg(LOG_ERROR, "select on the listener failed, error %d%s",
                    err, net_error_text(err));
            Sleep(1000);
        } else if (n > 0) {
            accept_one(ls, fn);
        }
    }

    slots_stop();
    closesocket(ls);
#ifdef NTRDPS_TLS
    tls_cleanup();
#endif
    WSACleanup();
    log_msg(LOG_INFO, "listener closed");
    return 0;
}
