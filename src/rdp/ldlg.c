/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "auth.h"
#include "core.h"
#include "desktop.h"
#include "keymap.h"
#include "ldlg.h"
#include "tls.h"

#define DESK_NAME    "ntrdps-login"
#define CLASS_NAME   "ntrdps login"
#define TIMER_ID     1
#define TIMER_MS     30
#define REDRAW_MS    1000
#define OPEN_WAIT_MS 20000
#define END_WAIT_MS  5000
#define EVT_MAX      256
#define KEYQ_MAX     16
#define TOPS_MAX     16
#define KIDS_MAX     64

#define WM_LD_WAKE   (WM_USER + 100)
#define WM_LD_NEXT   (WM_USER + 101)

#define X_WM_PRINT      0x0317
#define X_PRF_NONCLIENT 0x0002
#define X_DS_3DLOOK     0x0004L
#define X_STM_SETICON   0x0170
#define X_CB_FINDEXACT  0x0158
#define X_DWL_USER      8
#define X_OBM_CLOSE     32754
#define X_IMAGE_ICON    1
#define X_IMAGE_BITMAP  0
#define X_LR_CREATEDIBSECTION 0x2000
#define X_COLOR_INACTIVECAPTIONTEXT 19
#define X_COLOR_BTNHIGHLIGHT        20
#define X_OBM_COMBO     32738
#define X_CB_GETITEMHEIGHT 0x0154
#define X_ROP_ARROW     0x00B8074AL

#define LOOK_40 0
#define LOOK_3X 1
#define LOOK_50 2

#define DLG_40      1450
#define DLG_3X      200
#define DLG_50      1500
#define HELP_40     1950
#define HELP_3X     515
#define ICON_40     15
#define LOGO_3X     2
#define STR_CAPTION 1501
#define STR_WRONG   1505

#define I40_USER    1453
#define I40_PW      1454
#define I40_DOMAIN  1455
#define I40_DIALUP  1456
#define I40_DOMTEXT 1459
#define I40_HELP    1451
#define I40_SHUT    1452
#define I40_ICON    1461
#define I3X_USER    205
#define I3X_FROM    206
#define I3X_PW      207
#define I3X_HELP    208
#define I3X_HELP2   210
#define I3X_OK2     211
#define I3X_SHUT    300
#define I3X_LOGO    102
#define I50_SHUT    1501
#define I50_USER    1502
#define I50_PW      1503
#define I50_DOMAIN  1504
#define I50_DIALUP  1505
#define I50_USERTXT 1506
#define I50_PWTEXT  1507
#define I50_DOMTEXT 1508
#define I50_OPTIONS 1514
#define I50_LEGAL   2400
#define I50_KBD     2406
#define STR_MORE    1800
#define STR_LESS    1801
#define BMP_BAND8   103
#define BMP_BAND4   117

#define X_VK_LSHIFT 0xA0
#define X_VK_RMENU  0xA5
#define X_VK_LWIN   0x5B
#define X_VK_APPS   0x5D

#define EV_KEY   1
#define EV_LOCKS 2
#define EV_MOUSE 3

typedef struct {
    unsigned char  kind;
    unsigned char  code, prefix, down;
    unsigned short buttons;
    short          x, y;
} EVT;

typedef struct {
    unsigned int  vk, scan, flags;
    BYTE          keys[256];
} KEYMSG;

typedef struct {
    DWORD         min, max;
    DWORD         caret;
} SELST;

typedef struct {
    USHORT Length, MaximumLength;
    PWSTR  Buffer;
} X_LSA_STRING;

typedef struct {
    ULONG         Length;
    HANDLE        RootDirectory;
    X_LSA_STRING *ObjectName;
    ULONG         Attributes;
    PVOID         SecurityDescriptor;
    PVOID         SecurityQualityOfService;
} X_LSA_OA;

typedef struct {
    X_LSA_STRING Name;
    PSID         Sid;
} X_PRIMARY_DOMAIN;

typedef LONG (WINAPI *LSA_OPEN)(X_LSA_STRING *, X_LSA_OA *, DWORD,
                                HANDLE *);
typedef LONG (WINAPI *LSA_QUERY)(HANDLE, int, PVOID *);
typedef LONG (WINAPI *LSA_FREE)(PVOID);
typedef LONG (WINAPI *LSA_CLOSE)(HANDLE);
typedef HANDLE (WINAPI *LOAD_IMAGE)(HINSTANCE, LPCSTR, UINT, int, int,
                                    UINT);

struct LDLG_s {
    CRITICAL_SECTION lock;
    HANDLE           thread;
    HANDLE           ready;
    DWORD            tid;
    char             peer[64];
    unsigned int     width, height;
    WCHAR            init_user[AUTH_NAME_MAX + 1];
    WCHAR            init_domain[AUTH_NAME_MAX + 1];
    HWND             ctl;
    int              up;

    LDLG_WAKE        wake;
    void            *wake_ctx;
    EVT              q[EVT_MAX];
    unsigned int     q_first, q_count;
    int              q_full;
    int              event;
    WCHAR            user[LDLG_USER_MAX + 1];
    WCHAR            domain[AUTH_NAME_MAX + 1];
    WCHAR            pw[AUTH_PW_MAX + 1];
    int              fail;
    int              end;
    unsigned char   *pic;
    long             pic_stride;
    RECT             dirty;

    HMODULE          mod;
    int              look;
    HWND             dlg;
    int              id_user, id_pw, id_dom;
    int              busy;
    int              modal;
    int              checking;
    KEYMAP           km;
    BYTE             keys[256];
    KEYMSG           kq[KEYQ_MAX];
    unsigned int     kq_first, kq_count;
    unsigned int     buttons;
    HDC              mdc;
    HBITMAP          mbm, mold;
    int              mw, mh;
    unsigned char   *rows;
    RECT             last;
    DWORD            drawn_ms;
    int              redraw;
    int              phase;
    HWND             caret_wnd;
    POINT            caret_at;
    DWORD            caret_ms;
    HBITMAP          logo;
    HBITMAP          sysbox;
    HICON            icon;
    HBITMAP          brand, band;
    int              deep;
    int              self;
    SELST            sel[2];
    HBITMAP          arrow;
};

static int g_class;

static void wcopy(WCHAR *to, const WCHAR *from, unsigned int cap)
{
    unsigned int i;

    for (i = 0; i + 1 < cap && from[i] != 0; i++) {
        to[i] = from[i];
    }
    to[i] = 0;
}

static void wset(WCHAR *to, const char *from, unsigned int cap)
{
    unsigned int i;

    for (i = 0; i + 1 < cap && from[i] != '\0'; i++) {
        to[i] = (WCHAR) (unsigned char) from[i];
    }
    to[i] = 0;
}

static void join(RECT *r, const RECT *a)
{
    if (IsRectEmpty(a)) {
        return;
    }
    if (IsRectEmpty(r)) {
        *r = *a;
        return;
    }
    if (a->left < r->left) {
        r->left = a->left;
    }
    if (a->top < r->top) {
        r->top = a->top;
    }
    if (a->right > r->right) {
        r->right = a->right;
    }
    if (a->bottom > r->bottom) {
        r->bottom = a->bottom;
    }
}

static int ours(const LDLG *d, HWND w)
{
    return w != NULL && GetWindowThreadProcessId(w, NULL) == d->tid;
}

static int shown(HWND w)
{
    return (GetWindowLongA(w, GWL_STYLE) & WS_VISIBLE) != 0;
}

static int is_class(HWND w, const char *name)
{
    char cls[16];

    return w != NULL && GetClassNameA(w, cls, sizeof(cls)) &&
           lstrcmpiA(cls, name) == 0;
}

static void wake(LDLG *d)
{
    if (d->wake != NULL) {
        d->wake(d->wake_ctx);
    }
}

static void center(LDLG *d, HWND w)
{
    RECT r;

    GetWindowRect(w, &r);
    SetWindowPos(w, NULL, ((int) d->width - (r.right - r.left)) / 2,
                 ((int) d->height - (r.bottom - r.top)) / 2, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void *copy_template(LDLG *d, int id)
{
    HRSRC r = FindResourceA(d->mod, MAKEINTRESOURCE(id), RT_DIALOG);
    HGLOBAL g = r != NULL ? LoadResource(d->mod, r) : NULL;
    DWORD n = r != NULL ? SizeofResource(d->mod, r) : 0;
    unsigned char *t;
    DWORD *style;

    if (g == NULL || n < 18 || LockResource(g) == NULL) {
        return NULL;
    }
    t = (unsigned char *) malloc(n);
    if (t == NULL) {
        return NULL;
    }
    memcpy(t, LockResource(g), n);
    style = (DWORD *) (t + ((((WORD *) t)[0] == 1 &&
                             ((WORD *) t)[1] == 0xFFFF) ? 12 : 0));
    *style &= ~(DWORD) WS_VISIBLE;
    if (d->look != LOOK_3X) {
        *style |= X_DS_3DLOOK;
    }
    return t;
}

static int primary_domain(WCHAR *out, unsigned int cap)
{
    HMODULE adv = GetModuleHandleA("ADVAPI32.DLL");
    LSA_OPEN p_open;
    LSA_QUERY p_query;
    LSA_FREE p_free;
    LSA_CLOSE p_close;
    X_LSA_OA oa;
    HANDLE h;
    X_PRIMARY_DOMAIN *info = NULL;
    unsigned int n;
    int member = 0;

    out[0] = 0;
    if (adv == NULL) {
        return 0;
    }
    p_open = (LSA_OPEN) GetProcAddress(adv, "LsaOpenPolicy");
    p_query = (LSA_QUERY) GetProcAddress(adv, "LsaQueryInformationPolicy");
    p_free = (LSA_FREE) GetProcAddress(adv, "LsaFreeMemory");
    p_close = (LSA_CLOSE) GetProcAddress(adv, "LsaClose");
    if (p_open == NULL || p_query == NULL || p_free == NULL ||
        p_close == NULL) {
        return 0;
    }
    memset(&oa, 0, sizeof(oa));
    oa.Length = sizeof(oa);
    if (p_open(NULL, &oa, 0x00000001L ,
               &h) != 0) {
        return 0;
    }
    if (p_query(h, 3 ,
                (PVOID *) &info) == 0 && info != NULL) {
        if (info->Sid != NULL && info->Name.Buffer != NULL) {
            n = info->Name.Length / sizeof(WCHAR);
            if (n >= cap) {
                n = cap - 1;
            }
            memcpy(out, info->Name.Buffer, n * sizeof(WCHAR));
            out[n] = 0;
            member = n > 0;
        }
        p_free(info);
    }
    p_close(h);
    return member;
}

static void fill_domains(LDLG *d, HWND combo, int member, const WCHAR *dom)
{
    WCHAR comp[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    LRESULT i = CB_ERR;

    if (!GetComputerNameW(comp, &n)) {
        comp[0] = 0;
    }
    if (member) {
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM) dom);
    }
    if (comp[0] != 0) {
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM) comp);
    }
    if (d->init_domain[0] != 0) {
        i = SendMessageW(combo, X_CB_FINDEXACT, (WPARAM) -1,
                         (LPARAM) d->init_domain);
    }
    if (i == CB_ERR) {
        i = SendMessageW(combo, X_CB_FINDEXACT, (WPARAM) -1,
                         (LPARAM) (member ? dom : comp));
    }
    SendMessageW(combo, CB_SETCURSEL, i == CB_ERR ? 0 : (WPARAM) i, 0);
}

static BOOL CALLBACK help_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    LDLG *d;

    if (m == WM_INITDIALOG) {
        d = (LDLG *) lp;
        center(d, w);
        return TRUE;
    }
    if (m == WM_COMMAND && (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)) {
        EndDialog(w, 0);
        return TRUE;
    }
    return FALSE;
}

static void help(LDLG *d)
{
    void *t;

    if (d->modal) {
        return;
    }
    t = copy_template(d, d->look == LOOK_40 ? HELP_40 : HELP_3X);
    if (t == NULL) {
        return;
    }
    d->modal = 1;
    d->redraw = 1;
    DialogBoxIndirectParamW(d->mod, (LPDLGTEMPLATEW) t, d->dlg,
                            (DLGPROC) help_proc, (LPARAM) d);
    d->modal = 0;
    d->redraw = 1;
    free(t);
}

static void submit(LDLG *d)
{
    WCHAR user[LDLG_USER_MAX + 1], dom[AUTH_NAME_MAX + 1];
    WCHAR pw[AUTH_PW_MAX + 1];
    HWND c = d->id_dom != 0 ? GetDlgItem(d->dlg, d->id_dom) : NULL;

    if (d->checking || d->modal) {
        return;
    }
    user[0] = 0;
    dom[0] = 0;
    pw[0] = 0;
    GetDlgItemTextW(d->dlg, d->id_user, user, LDLG_USER_MAX + 1);
    GetDlgItemTextW(d->dlg, d->id_pw, pw, AUTH_PW_MAX + 1);
    if (c != NULL && shown(c)) {
        GetWindowTextW(c, dom, AUTH_NAME_MAX + 1);
    }
    d->checking = 1;
    EnterCriticalSection(&d->lock);
    wcopy(d->user, user, LDLG_USER_MAX + 1);
    wcopy(d->domain, dom, AUTH_NAME_MAX + 1);
    wcopy(d->pw, pw, AUTH_PW_MAX + 1);
    d->event = LDLG_SUBMIT;
    wake(d);
    LeaveCriticalSection(&d->lock);
    memset(pw, 0, sizeof(pw));
}

static void cancel(LDLG *d)
{
    if (d->checking || d->modal) {
        return;
    }
    d->checking = 1;
    EnterCriticalSection(&d->lock);
    d->event = LDLG_CANCEL;
    wake(d);
    LeaveCriticalSection(&d->lock);
}

static BOOL CALLBACK dlg_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
#ifdef _WIN64
    LDLG *d = (LDLG *) GetWindowLongPtrA(w, DWLP_USER);
#else
    LDLG *d = (LDLG *) GetWindowLongA(w, X_DWL_USER);
#endif

    switch (m) {
    case WM_INITDIALOG:
#ifdef _WIN64
        SetWindowLongPtrA(w, DWLP_USER, (LONG_PTR) lp);
#else
        SetWindowLongA(w, X_DWL_USER, (LONG) lp);
#endif
        return FALSE;
    case WM_COMMAND:
        if (d == NULL || HIWORD(wp) != BN_CLICKED) {
            break;
        }
        switch (LOWORD(wp)) {
        case IDOK:
        case I3X_OK2:
            submit(d);
            return TRUE;
        case IDCANCEL:
            cancel(d);
            return TRUE;
        case I40_HELP:
        case I3X_HELP:
        case I3X_HELP2:
            help(d);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void show_fail(LDLG *d)
{
    WCHAR text[512], cap[128];
    HWND pw;

    if (LoadStringW(d->mod, STR_WRONG, text, 512) <= 0) {
        wset(text, "Wrong user name or password.", 512);
    }
    if (LoadStringW(d->mod, STR_CAPTION, cap, 128) <= 0) {
        wset(cap, "Logon Message", 128);
    }
    d->modal = 1;
    d->checking = 0;
    d->redraw = 1;
    MessageBoxW(d->dlg, text, cap, MB_OK | MB_ICONEXCLAMATION);
    d->modal = 0;
    d->redraw = 1;
    if (d->end || !IsWindow(d->dlg)) {
        return;
    }
    pw = GetDlgItem(d->dlg, d->id_pw);
    SetWindowTextW(pw, L"");
    SetFocus(pw);
}

static void move_ids(HWND dlg, const int *ids, int n, int dx, int dy,
                     int size)
{
    HWND c;
    RECT r;
    int i;

    for (i = 0; i < n; i++) {
        if ((c = GetDlgItem(dlg, ids[i])) != NULL) {
            GetWindowRect(c, &r);
            MapWindowPoints(NULL, dlg, (POINT *) &r, 2);
            SetWindowPos(c, NULL, r.left + dx, r.top + dy, 0, 0,
                         SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    if (size) {
        GetWindowRect(dlg, &r);
        SetWindowPos(dlg, NULL, 0, 0, r.right - r.left + dx,
                     r.bottom - r.top + dy,
                     SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    }
}

static void item_rect(HWND dlg, int id, RECT *r)
{
    HWND c = GetDlgItem(dlg, id);

    SetRectEmpty(r);
    if (c != NULL) {
        GetWindowRect(c, r);
    }
}

static void hide_id(HWND dlg, int id)
{
    HWND c = GetDlgItem(dlg, id);

    if (c != NULL) {
        ShowWindow(c, SW_HIDE);
        EnableWindow(c, FALSE);
    }
}

static DWORD winlogon_dword(char *name)
{
    HKEY k;
    DWORD v = 0, n = sizeof(v), type;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT"
                      "\\CurrentVersion\\Winlogon", 0, KEY_READ, &k) !=
        ERROR_SUCCESS) {
        return 0;
    }
    if (RegQueryValueExA(k, name, NULL, &type, (LPBYTE) &v, &n) !=
        ERROR_SUCCESS || type != REG_DWORD) {
        v = 0;
    }
    RegCloseKey(k);
    return v;
}

static int product(void)
{
    HKEY k;
    char buf[256];
    DWORD n = sizeof(buf) - 2, type;
    const char *s;
    int p = 0;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\"
                      "Control\\ProductOptions", 0, KEY_READ, &k) !=
        ERROR_SUCCESS) {
        return 0;
    }
    if (RegQueryValueExA(k, "ProductType", NULL, &type, (LPBYTE) buf,
                         &n) == ERROR_SUCCESS && type == REG_SZ) {
        buf[n] = 0;
        if (lstrcmpiA(buf, "WinNT") != 0) {
            p = 1;
        }
    }
    n = sizeof(buf) - 2;
    if (p == 1 && RegQueryValueExA(k, "ProductSuite", NULL, &type,
                                   (LPBYTE) buf, &n) == ERROR_SUCCESS &&
        type == REG_MULTI_SZ) {
        buf[n] = 0;
        buf[n + 1] = 0;
        for (s = buf; *s != 0; s += strlen(s) + 1) {
            if (lstrcmpiA(s, "DataCenter") == 0) {
                p = 3;
            } else if (lstrcmpiA(s, "Enterprise") == 0 && p < 3) {
                p = 2;
            }
        }
    }
    RegCloseKey(k);
    return p;
}

static HBITMAP load_bmp(LDLG *d, int id)
{
    LOAD_IMAGE p_load;
    HBITMAP b = NULL;

    if (id == 0 ||
        FindResourceA(d->mod, MAKEINTRESOURCE(id), RT_BITMAP) == NULL) {
        return NULL;
    }
    p_load = (LOAD_IMAGE) GetProcAddress(GetModuleHandleA("USER32.DLL"),
                                         "LoadImageA");
    if (p_load != NULL) {
        b = (HBITMAP) p_load(d->mod, MAKEINTRESOURCE(id), X_IMAGE_BITMAP,
                             0, 0, X_LR_CREATEDIBSECTION);
    }
    if (b == NULL) {
        b = LoadBitmapA(d->mod, MAKEINTRESOURCE(id));
    }
    return b;
}

static void setup_50(LDLG *d, int member)
{
    static const int all[12] = {
        I50_USERTXT, I50_USER, I50_PWTEXT, I50_PW, I50_DOMTEXT, I50_DOMAIN,
        I50_DIALUP, I50_KBD, IDOK, IDCANCEL, I50_SHUT, I50_OPTIONS
    };
    static const int nodom[6] = {
        IDOK, IDCANCEL, I50_SHUT, I50_OPTIONS, I50_DIALUP, I50_KBD
    };
    static const int buttons[5] = {
        IDOK, IDCANCEL, I50_SHUT, I50_OPTIONS, I50_KBD
    };
    static const int banner[4][2] = {
        { 107, 111 }, { 0, 112 }, { 0, 119 }, { 0, 122 }
    };
    HWND dlg = d->dlg, c;
    RECT r, r2;
    WCHAR text[32];
    BITMAP b1, b2;
    HDC dc;
    int opts = member || winlogon_dword("ShowLogonOptions") != 0;
    int p, dy;

    item_rect(dlg, I50_LEGAL, &r);
    move_ids(dlg, all, 12, 0, -(int) (r.bottom - r.top), 1);
    hide_id(dlg, I50_LEGAL);
    if (!member) {
        hide_id(dlg, I50_DOMTEXT);
        hide_id(dlg, I50_DOMAIN);
        item_rect(dlg, I50_PW, &r);
        item_rect(dlg, I50_DOMAIN, &r2);
        move_ids(dlg, nodom, 6, 0, -(int) (r2.bottom - r.bottom), 1);
    }
    hide_id(dlg, I50_DIALUP);
    hide_id(dlg, I50_KBD);
    item_rect(dlg, member ? I50_DOMAIN : I50_PW, &r);
    item_rect(dlg, I50_DIALUP, &r2);
    move_ids(dlg, buttons, 5, 0, -(int) (r2.bottom - r.bottom), 1);
    if (!opts) {
        item_rect(dlg, I50_SHUT, &r);
        item_rect(dlg, I50_OPTIONS, &r2);
        hide_id(dlg, I50_SHUT);
        move_ids(dlg, buttons, 2, (int) (r2.left - r.left), 0, 0);
    } else if ((c = GetDlgItem(dlg, I50_SHUT)) != NULL) {
        EnableWindow(c, FALSE);
    }
    if ((c = GetDlgItem(dlg, I50_OPTIONS)) != NULL) {
        if (LoadStringW(d->mod, opts ? STR_LESS : STR_MORE, text, 32) > 0) {
            SetWindowTextW(c, text);
        }
        EnableWindow(c, FALSE);
    }

    dc = CreateCompatibleDC(NULL);
    d->deep = dc != NULL && GetDeviceCaps(dc, BITSPIXEL) > 8;
    if (dc != NULL) {
        DeleteDC(dc);
    }
    p = product();
    d->brand = d->deep ? load_bmp(d, banner[p][0]) : NULL;
    if (d->brand == NULL) {
        d->brand = load_bmp(d, banner[p][1]);
    }
    d->band = load_bmp(d, d->deep ? BMP_BAND8 : BMP_BAND4);
    if (d->brand == NULL || d->band == NULL ||
        GetObjectA(d->brand, sizeof(b1), &b1) == 0 ||
        GetObjectA(d->band, sizeof(b2), &b2) == 0) {
        log_msg(LOG_WARN, "session %s: login: no banner in msgina.dll",
                d->peer);
        if (d->brand != NULL) {
            DeleteObject(d->brand);
        }
        if (d->band != NULL) {
            DeleteObject(d->band);
        }
        d->brand = NULL;
        d->band = NULL;
        return;
    }
    dy = (int) (b1.bmHeight + b2.bmHeight);
    for (c = GetWindow(dlg, GW_CHILD); c != NULL;
         c = GetWindow(c, GW_HWNDNEXT)) {
        GetWindowRect(c, &r);
        MapWindowPoints(NULL, dlg, (POINT *) &r, 2);
        SetWindowPos(c, NULL, r.left, r.top + dy, 0, 0,
                     SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    GetWindowRect(dlg, &r);
    SetWindowPos(dlg, NULL, 0, 0, r.right - r.left, r.bottom - r.top + dy,
                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
}

static void paint_brand(LDLG *d, int cx)
{
    BITMAP b1, b2;
    HDC bdc;
    HGDIOBJ old;
    HBRUSH br;
    RECT r;
    int x, w;

    if (GetObjectA(d->brand, sizeof(b1), &b1) == 0 ||
        GetObjectA(d->band, sizeof(b2), &b2) == 0 ||
        (bdc = CreateCompatibleDC(d->mdc)) == NULL) {
        return;
    }
    old = SelectObject(bdc, d->band);
    w = b2.bmWidth < cx ? (int) b2.bmWidth : cx;
    StretchBlt(d->mdc, 0, (int) b1.bmHeight, cx, (int) b2.bmHeight, bdc,
               ((int) b2.bmWidth - w) / 2, 0, w, (int) b2.bmHeight, SRCCOPY);
    SelectObject(bdc, d->brand);
    x = (cx - (int) b1.bmWidth) / 2;
    if (x < 0) {
        x = 0;
    }
    BitBlt(d->mdc, x, 0, (int) b1.bmWidth, (int) b1.bmHeight, bdc, 0, 0,
           SRCCOPY);
    if (x != 0) {
        br = CreateSolidBrush(d->deep ? RGB(90, 124, 223) : RGB(0, 0, 128));
        if (br != NULL) {
            SetRect(&r, 0, 0, x, (int) b1.bmHeight);
            FillRect(d->mdc, &r, br);
            SetRect(&r, cx - x - 1, 0, cx, (int) b1.bmHeight);
            FillRect(d->mdc, &r, br);
            DeleteObject(br);
        }
    }
    SelectObject(bdc, old);
    DeleteDC(bdc);
}

static int setup_dialog(LDLG *d)
{
    LOAD_IMAGE p_load;
    WCHAR dom[AUTH_NAME_MAX + 1];
    HWND w, user, pw;
    char cls[16];
    int member = primary_domain(dom, AUTH_NAME_MAX + 1);

    if (d->look == LOOK_40) {
        d->id_user = I40_USER;
        d->id_pw = I40_PW;
        d->id_dom = I40_DOMAIN;
    } else if (d->look == LOOK_50) {
        d->id_user = I50_USER;
        d->id_pw = I50_PW;
        d->id_dom = I50_DOMAIN;
    } else {
        d->id_user = I3X_USER;
        d->id_pw = I3X_PW;
        d->id_dom = I3X_FROM;
    }
    user = GetDlgItem(d->dlg, d->id_user);
    pw = GetDlgItem(d->dlg, d->id_pw);
    if (user == NULL || pw == NULL || !GetClassNameA(pw, cls, sizeof(cls)) ||
        lstrcmpiA(cls, "Edit") != 0 ||
        !(GetWindowLongA(pw, GWL_STYLE) & ES_PASSWORD)) {
        log_msg(LOG_WARN, "session %s: login: the logon dialog has not the "
                "expected fields", d->peer);
        return 0;
    }
    SendMessageW(user, EM_LIMITTEXT, LDLG_USER_MAX, 0);
    SendMessageW(pw, EM_LIMITTEXT, AUTH_PW_MAX, 0);
    if (d->look == LOOK_40) {
        if ((w = GetDlgItem(d->dlg, I40_DIALUP)) != NULL) {
            ShowWindow(w, SW_HIDE);
        }
        if ((w = GetDlgItem(d->dlg, I40_SHUT)) != NULL) {
            EnableWindow(w, FALSE);
        }
        if (!member) {
            if ((w = GetDlgItem(d->dlg, I40_DOMAIN)) != NULL) {
                ShowWindow(w, SW_HIDE);
            }
            if ((w = GetDlgItem(d->dlg, I40_DOMTEXT)) != NULL) {
                ShowWindow(w, SW_HIDE);
            }
        }
        p_load = (LOAD_IMAGE) GetProcAddress(GetModuleHandleA("USER32.DLL"),
                                             "LoadImageA");
        if (p_load != NULL &&
            (w = GetDlgItem(d->dlg, I40_ICON)) != NULL) {
            d->icon = (HICON) p_load(d->mod, MAKEINTRESOURCE(ICON_40),
                                     X_IMAGE_ICON, 64, 64, 0);
            if (d->icon != NULL) {
                SendMessageW(w, X_STM_SETICON, (WPARAM) d->icon, 0);
            }
        }
    } else if (d->look == LOOK_50) {
        setup_50(d, member);
    } else {
        if (GetDlgItem(d->dlg, I3X_SHUT) != NULL) {
            if ((w = GetDlgItem(d->dlg, I3X_SHUT)) != NULL) {
                ShowWindow(w, SW_HIDE);
            }
            if ((w = GetDlgItem(d->dlg, I3X_OK2)) != NULL) {
                ShowWindow(w, SW_HIDE);
            }
            if ((w = GetDlgItem(d->dlg, I3X_HELP2)) != NULL) {
                ShowWindow(w, SW_HIDE);
            }
            SendMessageW(d->dlg, DM_SETDEFID, IDOK, 0);
        }
        d->logo = LoadBitmapA(d->mod, MAKEINTRESOURCE(LOGO_3X));
    }
    if ((w = GetDlgItem(d->dlg, d->id_dom)) != NULL && shown(w)) {
        fill_domains(d, w, member, dom);
    }
    SetWindowTextW(user, d->init_user);
    return 1;
}

static void emit(void *ctx, unsigned int vk, unsigned int scan,
                 unsigned int flags)
{
    LDLG *d = (LDLG *) ctx;
    int down = !(flags & KM_UP);
    unsigned int plain = 0;
    KEYMSG *k;

    if (vk >= X_VK_LSHIFT && vk <= X_VK_RMENU) {
        plain = vk <= 0xA1 ? VK_SHIFT : vk <= 0xA3 ? VK_CONTROL : VK_MENU;
        d->keys[vk] = (BYTE) (down ? 0x80 : 0);
        d->keys[plain] = (BYTE) ((d->keys[vk] | d->keys[vk ^ 1]) & 0x80);
        return;
    }
    vk &= 0xFF;
    if (down && (vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL)) {
        d->keys[vk] ^= 1;
    }
    d->keys[vk] = (BYTE) ((d->keys[vk] & 1) | (down ? 0x80 : 0));
    if (vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL ||
        d->kq_count >= KEYQ_MAX) {
        return;
    }
    k = &d->kq[(d->kq_first + d->kq_count++) % KEYQ_MAX];
    k->vk = vk;
    k->scan = scan;
    k->flags = flags;
    memcpy(k->keys, d->keys, sizeof(k->keys));
}

static unsigned int map_vk(void *ctx, unsigned int code)
{
    return MapVirtualKeyA(code, 1);
}

static void sas(void *ctx)
{
}

static int dropped(unsigned int vk, int shift, int ctrl, int alt)
{
    if (vk == VK_F1 || vk == VK_F10 || vk == VK_SNAPSHOT || vk == VK_CANCEL ||
        vk == VK_PAUSE || (vk >= X_VK_LWIN && vk <= X_VK_APPS)) {
        return 1;
    }
    if (alt && !ctrl) {
        return !((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') ||
                 (vk >= VK_LEFT && vk <= VK_DOWN) || vk == VK_F4);
    }
    if (ctrl && !alt) {
        return vk == 'C' || vk == 'V' || vk == 'X' || vk == VK_INSERT ||
               vk == VK_ESCAPE;
    }
    return shift && (vk == VK_INSERT || vk == VK_DELETE);
}

static int post_key(LDLG *d, const KEYMSG *k)
{
    int up = (k->flags & KM_UP) != 0;
    int shift = (k->keys[VK_SHIFT] & 0x80) != 0;
    int ctrl = (k->keys[VK_CONTROL] & 0x80) != 0;
    int alt = (k->keys[VK_MENU] & 0x80) != 0;
    HWND f;
    UINT msg;
    LPARAM lp;

    SetKeyboardState((LPBYTE) k->keys);
    f = GetFocus();
    if (d->checking || !ours(d, f) || dropped(k->vk, shift, ctrl, alt)) {
        return 0;
    }
    if (d->self && is_class(f, "ComboBox") &&
        (k->vk == VK_F4 || (alt && (k->vk == VK_UP || k->vk == VK_DOWN)))) {
        return 0;
    }
    lp = 1 | ((LPARAM) (k->scan & 0xFF) << 16);
    if (k->flags & KM_EXTENDED) {
        lp |= 1L << 24;
    }
    if (alt && !ctrl) {
        lp |= 1L << 29;
        msg = up ? WM_SYSKEYUP : WM_SYSKEYDOWN;
    } else {
        msg = up ? WM_KEYUP : WM_KEYDOWN;
    }
    if (up) {
        lp |= 3L << 30;
    }
    PostMessageW(f, msg, (WPARAM) k->vk, lp);
    return 1;
}

static int tops(LDLG *d, HWND *out, int max)
{
    HWND w;
    int n = 0;

    if (d->dlg == NULL || !IsWindow(d->dlg)) {
        return 0;
    }
    for (w = GetWindow(d->dlg, GW_HWNDFIRST); w != NULL && n < max;
         w = GetWindow(w, GW_HWNDNEXT)) {
        if (shown(w) && ours(d, w)) {
            out[n++] = w;
        }
    }
    return n;
}

static HWND hit(LDLG *d, POINT p)
{
    HWND t[TOPS_MAX], w, c;
    RECT r;
    int n = tops(d, t, TOPS_MAX), i;

    for (i = 0; i < n; i++) {
        GetWindowRect(t[i], &r);
        if (!PtInRect(&r, p)) {
            continue;
        }
        if (!IsWindowEnabled(t[i])) {
            return NULL;
        }
        w = t[i];
        for (;;) {
            for (c = GetWindow(w, GW_CHILD); c != NULL;
                 c = GetWindow(c, GW_HWNDNEXT)) {
                GetWindowRect(c, &r);
                if (shown(c) && PtInRect(&r, p)) {
                    break;
                }
            }
            if (c == NULL) {
                break;
            }
            if (!IsWindowEnabled(c)) {
                return NULL;
            }
            w = c;
        }
        if (w == t[i] &&
            (GetWindowLongA(w, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) {
            return NULL;
        }
        return w;
    }
    return NULL;
}

static int mouse(LDLG *d, const EVT *e)
{
    int was = (d->buttons & CORE_BUTTON_LEFT) != 0;
    int now = (e->buttons & CORE_BUTTON_LEFT) != 0;
    POINT p;
    HWND w;
    UINT msg;

    d->buttons = e->buttons;
    if (d->checking || (!was && !now)) {
        return 0;
    }
    p.x = e->x;
    p.y = e->y;
    w = GetCapture();
    if (!ours(d, w)) {
        w = hit(d, p);
    }
    if (w == NULL) {
        return 0;
    }
    if (d->self && is_class(w, "ComboBox")) {
        if (!was) {
            SetFocus(w);
            d->redraw = 1;
        }
        return 0;
    }
    msg = !was ? WM_LBUTTONDOWN : !now ? WM_LBUTTONUP : WM_MOUSEMOVE;
    ScreenToClient(w, &p);
    PostMessageW(w, msg, now ? MK_LBUTTON : 0,
                 MAKELPARAM((WORD) (short) p.x, (WORD) (short) p.y));
    return 1;
}

static int take_event(LDLG *d, EVT *e)
{
    int got = 0;

    EnterCriticalSection(&d->lock);
    if (d->q_count > 0) {
        *e = d->q[d->q_first];
        d->q_first = (d->q_first + 1) % EVT_MAX;
        d->q_count--;
        got = 1;
    }
    LeaveCriticalSection(&d->lock);
    return got;
}

static void pump(LDLG *d)
{
    EVT e;
    int posted;

    while (!d->busy) {
        posted = 0;
        if (d->kq_count > 0) {
            posted = post_key(d, &d->kq[d->kq_first]);
            d->kq_first = (d->kq_first + 1) % KEYQ_MAX;
            d->kq_count--;
        } else if (!take_event(d, &e)) {
            return;
        } else if (e.kind == EV_KEY) {
            keymap_key(&d->km, e.code, e.prefix, e.down);
        } else if (e.kind == EV_LOCKS) {
            d->keys[VK_CAPITAL] = (BYTE) ((d->keys[VK_CAPITAL] & 0x80) |
                                  ((e.buttons & CORE_LOCK_CAPS) ? 1 : 0));
            d->keys[VK_NUMLOCK] = (BYTE) ((d->keys[VK_NUMLOCK] & 0x80) |
                                  ((e.buttons & CORE_LOCK_NUM) ? 1 : 0));
            d->keys[VK_SCROLL] = (BYTE) ((d->keys[VK_SCROLL] & 0x80) |
                                 ((e.buttons & CORE_LOCK_SCROLL) ? 1 : 0));
            d->km.numlock = (e.buttons & CORE_LOCK_NUM) != 0;
            SetKeyboardState(d->keys);
        } else {
            posted = mouse(d, &e);
        }
        if (posted) {
            d->busy = 1;
            d->redraw = 1;
            PostMessageA(d->ctl, WM_LD_NEXT, 0, 0);
        }
    }
}

static void fill(HDC dc, int x0, int y0, int x1, int y1, int color)
{
    RECT r;
    HBRUSH b = CreateSolidBrush(GetSysColor(color));

    r.left = x0;
    r.top = y0;
    r.right = x1;
    r.bottom = y1;
    if (b != NULL) {
        FillRect(dc, &r, b);
        DeleteObject(b);
    }
}

static void frame(HDC dc, const RECT *r, int cx, int cy, int color)
{
    fill(dc, r->left, r->top, r->left + cx, r->bottom - cy, color);
    fill(dc, r->left + cx, r->top, r->right, r->top + cy, color);
    fill(dc, r->left, r->bottom - cy, r->right - cx, r->bottom, color);
    fill(dc, r->right - cx, r->top + cy, r->right, r->bottom, color);
}

static void frame3x(LDLG *d, HDC dc, HWND w, const RECT *wr, int active)
{
    DWORD style = (DWORD) GetWindowLongA(w, GWL_STYLE);
    DWORD ex = (DWORD) GetWindowLongA(w, GWL_EXSTYLE);
    int cxb = GetSystemMetrics(SM_CXBORDER), cyb = GetSystemMetrics(SM_CYBORDER);
    int cxs = GetSystemMetrics(SM_CXSIZE), cys = GetSystemMetrics(SM_CYSIZE);
    int cap = GetSystemMetrics(SM_CYCAPTION) - cyb;
    int dlg = (ex & WS_EX_DLGMODALFRAME) ||
              (style & WS_CAPTION) == WS_DLGFRAME;
    int cx, cy, len;
    WCHAR text[128];
    RECT r, t;
    SIZE size;
    TEXTMETRICA tm;
    HDC bdc;
    HGDIOBJ old;

    r.left = 0;
    r.top = 0;
    r.right = wr->right - wr->left;
    r.bottom = wr->bottom - wr->top;
    if ((style & WS_CAPTION) != 0 || (ex & WS_EX_DLGMODALFRAME)) {
        frame(dc, &r, cxb, cyb, COLOR_WINDOWFRAME);
    }
    if (dlg) {
        InflateRect(&r, -cxb, -cyb);
        frame(dc, &r, 4 * cxb, 4 * cyb,
              active ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION);
        InflateRect(&r, -4 * cxb, -4 * cyb);
        if ((style & WS_CAPTION) != WS_CAPTION) {
            return;
        }
        r.top -= cyb;
        fill(dc, r.left, r.top, r.right, r.top + cap, COLOR_WINDOW);
    } else if ((style & WS_CAPTION) != WS_CAPTION) {
        return;
    }
    r.left += cxb;
    r.bottom = r.top + cap;
    fill(dc, r.left, r.bottom, r.right - cxb, r.bottom + cyb,
         COLOR_WINDOWFRAME);
    if (style & WS_SYSMENU) {
        fill(dc, r.left + cxs, r.top + cyb, r.left + cxs + cxb, r.top + cap,
             COLOR_WINDOWFRAME);
        if (d->sysbox == NULL) {
            d->sysbox = LoadBitmapA(NULL, MAKEINTRESOURCE(X_OBM_CLOSE));
        }
        bdc = d->sysbox != NULL ? CreateCompatibleDC(dc) : NULL;
        if (bdc != NULL) {
            old = SelectObject(bdc, d->sysbox);
            SetBkColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            SetTextColor(dc, GetSysColor(COLOR_WINDOW));
            BitBlt(dc, r.left, r.top + cyb, cxs, cys, bdc, 0, 0, SRCCOPY);
            SelectObject(bdc, old);
            DeleteDC(bdc);
        }
        r.left += cxs + cxb;
    }
    r.right -= cxb;
    len = GetWindowTextW(w, text, 128);
    old = SelectObject(dc, GetStockObject(SYSTEM_FONT));
    GetTextMetricsA(dc, &tm);
    size.cx = 0;
    if (len > 0) {
        GetTextExtentPointW(dc, text, len, &size);
    }
    cy = (cap - (int) tm.tmHeight - 1) / 2;
    if (cy < 0) {
        cy = 0;
    }
    cx = ((r.right - r.left) - (int) size.cx) / 2 - cxb;
    if (cx < 0) {
        cx = 0;
    }
    r.top += cyb;
    if (r.right > r.left) {
        fill(dc, r.left, r.top, r.right, r.bottom,
             active ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION);
    }
    if (len > 0 && r.right > r.left) {
        t = r;
        InflateRect(&t, -cx, 0);
        SetTextColor(dc, GetSysColor(active ? COLOR_CAPTIONTEXT
                                     : X_COLOR_INACTIVECAPTIONTEXT));
        SetBkMode(dc, TRANSPARENT);
        ExtTextOutW(dc, t.left + cxb, r.top + cy, ETO_CLIPPED, &t, text,
                    (UINT) len, NULL);
    }
    SelectObject(dc, old);
}

static void track(LDLG *d)
{
    HWND w;
    DWORD a, b;
    SELST *e;
    int i;

    for (i = 0; i < 2; i++) {
        w = GetDlgItem(d->dlg, i == 0 ? d->id_user : d->id_pw);
        if (w == NULL) {
            continue;
        }
        a = 0;
        b = 0;
        SendMessageW(w, EM_GETSEL, (WPARAM) &a, (LPARAM) &b);
        e = &d->sel[i];
        if (a == b) {
            e->caret = a;
        } else if (a == e->min && b != e->max) {
            e->caret = b;
        } else if (b == e->max && a != e->min) {
            e->caret = a;
        } else if (a != e->min) {
            e->caret = b;
        }
        e->min = a;
        e->max = b;
    }
}

static int extent(HDC dc, const WCHAR *text, int n)
{
    SIZE s;

    if (n <= 0 || !GetTextExtentPointW(dc, text, n, &s)) {
        return 0;
    }
    return (int) s.cx;
}

static int fit(HDC dc, const WCHAR *text, int n, int width, int pw)
{
    int lo = 0, hi = n + 1, mid;

    if (width <= 0 || n <= 0) {
        return 0;
    }
    if (pw > 0) {
        return width / pw < n ? width / pw : n;
    }
    while (lo < hi - 1) {
        mid = lo + ((hi - lo) / 2 > 1 ? (hi - lo) / 2 : 1);
        if (extent(dc, text, mid) > width) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    return lo;
}

static int ave_width(HDC dc)
{
    static const WCHAR abc[] =
        L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    TEXTMETRICA tm;

    GetTextMetricsA(dc, &tm);
    if (tm.tmPitchAndFamily & 1) {
        return (extent(dc, abc, 52) / 26 + 1) / 2;
    }
    return (int) tm.tmAveCharWidth;
}

typedef struct {
    WCHAR text[AUTH_PW_MAX + 2];
    int   n, first;
    WCHAR pc;
    int   pw;
    int   overhang;
    RECT  fmt;
    HGDIOBJ old;
} EDIT;

static void edit_begin(HDC dc, HWND w, EDIT *e)
{
    HFONT font = (HFONT) SendMessageW(w, WM_GETFONT, 0, 0);
    TEXTMETRICA tm;

    e->old = SelectObject(dc, font != NULL ? font
                              : GetStockObject(SYSTEM_FONT));
    GetTextMetricsA(dc, &tm);
    e->overhang = (int) tm.tmOverhang;
    e->text[0] = 0;
    e->pc = (WCHAR) SendMessageW(w, EM_GETPASSWORDCHAR, 0, 0);
    e->pw = 0;
    if (e->pc != 0) {
        e->n = GetWindowTextLengthW(w);
        e->pw = extent(dc, &e->pc, 1);
        if (e->pw < 1) {
            e->pw = 1;
        }
    } else {
        e->n = GetWindowTextW(w, e->text, AUTH_PW_MAX + 2);
    }
    if (e->n < 0 || e->n > AUTH_PW_MAX) {
        e->n = 0;
    }
    e->first = (int) SendMessageW(w, EM_GETFIRSTVISIBLELINE, 0, 0);
    if (e->first < 0 || e->first > e->n) {
        e->first = 0;
    }
    SetRectEmpty(&e->fmt);
    SendMessageW(w, EM_GETRECT, 0, (LPARAM) &e->fmt);
}

static void edit_end(HDC dc, EDIT *e)
{
    SelectObject(dc, e->old);
    memset(e->text, 0, sizeof(e->text));
}

static int edit_x(HDC dc, const EDIT *e, int i)
{
    if (i <= e->first) {
        return e->fmt.left;
    }
    if (e->pw > 0) {
        return e->fmt.left + (i - e->first) * e->pw;
    }
    return e->fmt.left + extent(dc, e->text + e->first, i - e->first) -
           e->overhang;
}

static void edit_part(HDC dc, const EDIT *e, int x0, int x1, int at,
                      int cnt, int on, int gray)
{
    COLORREF fg = GetSysColor(on ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
    COLORREF bg = GetSysColor(on ? COLOR_HIGHLIGHT : COLOR_WINDOW);
    RECT clip;
    int x = edit_x(dc, e, at), k;

    if (x0 >= x1) {
        return;
    }
    if (gray && GetSysColor(COLOR_GRAYTEXT) != bg) {
        fg = GetSysColor(COLOR_GRAYTEXT);
    }
    fill(dc, x0, e->fmt.top, x1, e->fmt.bottom + 1,
         on ? COLOR_HIGHLIGHT : COLOR_WINDOW);
    SetRect(&clip, x0, e->fmt.top, x1, e->fmt.bottom);
    SetTextColor(dc, fg);
    SetBkColor(dc, bg);
    if (e->pc != 0) {
        SetBkMode(dc, TRANSPARENT);
        for (k = 0; k < cnt; k++) {
            ExtTextOutW(dc, x, e->fmt.top, ETO_CLIPPED, &clip, &e->pc, 1,
                        NULL);
            x += e->pw;
        }
        SetBkMode(dc, OPAQUE);
    } else if (cnt > 0) {
        ExtTextOutW(dc, x, e->fmt.top, ETO_CLIPPED, &clip, e->text + at,
                    (UINT) cnt, NULL);
    }
}

static void draw_edit(LDLG *d, HWND w, const SELST *sel)
{
    EDIT e;
    RECT cr;
    int cxb = GetSystemMetrics(SM_CXBORDER);
    int cyb = GetSystemMetrics(SM_CYBORDER);
    int end, lo, hi, at, cnt, on, x0, x1, none;

    edit_begin(d->mdc, w, &e);
    GetClientRect(w, &cr);
    fill(d->mdc, cr.left, cr.top, cr.right, cr.bottom, COLOR_WINDOW);
    if (e.fmt.left > 0 || e.fmt.top > 0) {
        frame(d->mdc, &cr, cxb, cyb, COLOR_WINDOWFRAME);
        InflateRect(&cr, -cxb, -cyb);
    }
    IntersectClipRect(d->mdc, cr.left, cr.top, cr.right, cr.bottom);
    SetBkMode(d->mdc, OPAQUE);
    end = e.first + fit(d->mdc, e.text + e.first, e.n - e.first,
                        e.fmt.right - e.fmt.left, e.pw);
    lo = sel->min < (DWORD) e.n ? (int) sel->min : e.n;
    hi = sel->max < (DWORD) e.n ? (int) sel->max : e.n;
    none = lo >= hi || (GetFocus() != w &&
                        !(GetWindowLongA(w, GWL_STYLE) & ES_NOHIDESEL));
    x0 = e.fmt.left;
    for (at = e.first; at < end; at += cnt) {
        on = 0;
        if (none || at >= hi) {
            cnt = end - at;
        } else if (at >= lo) {
            on = 1;
            cnt = (end < hi ? end : hi) - at;
        } else {
            cnt = (end < lo ? end : lo) - at;
        }
        x1 = at + cnt == end && !on ? e.fmt.right
             : e.pw > 0 ? x0 + cnt * e.pw
             : x0 + extent(d->mdc, e.text + at, cnt) -
               (at + cnt == end ? 0 : e.overhang);
        edit_part(d->mdc, &e, x0, x1, at, cnt, on, !IsWindowEnabled(w));
        x0 = x1;
    }
    edit_end(d->mdc, &e);
}

static int edit_caret(LDLG *d, HWND w, POINT *p)
{
    EDIT e;
    HGDIOBJ old;
    const SELST *sel;
    int i, ave, sys, x;

    if (w == GetDlgItem(d->dlg, d->id_user)) {
        sel = &d->sel[0];
    } else if (w == GetDlgItem(d->dlg, d->id_pw)) {
        sel = &d->sel[1];
    } else {
        return 0;
    }
    old = SelectObject(d->mdc, GetStockObject(SYSTEM_FONT));
    sys = ave_width(d->mdc);
    SelectObject(d->mdc, old);
    edit_begin(d->mdc, w, &e);
    ave = ave_width(d->mdc);
    i = sel->caret < (DWORD) e.n ? (int) sel->caret : e.n;
    if (e.pw > 0) {
        x = e.fmt.left + (i - e.first) * e.pw;
    } else if (i < e.first) {
        x = e.fmt.left - extent(d->mdc, e.text + i, e.first - i) -
            e.overhang;
    } else {
        x = e.fmt.left + extent(d->mdc, e.text + e.first, i - e.first) -
            e.overhang;
    }
    if (x > e.fmt.right - (sys > ave ? 1 : 2)) {
        x = e.fmt.right - (sys > ave ? 1 : 2);
    }
    p->x = x;
    p->y = e.fmt.top;
    edit_end(d->mdc, &e);
    return 1;
}

static void combo_button(HDC dc, const RECT *r)
{
    int cxb = GetSystemMetrics(SM_CXBORDER);
    int cyb = GetSystemMetrics(SM_CYBORDER);
    RECT in = *r;
    int k;

    frame(dc, r, cxb, cyb, COLOR_WINDOWFRAME);
    InflateRect(&in, -cxb, -cyb);
    if (GetSysColor(COLOR_BTNSHADOW) != RGB(255, 255, 255)) {
        fill(dc, in.left, in.top, in.left + cxb, in.bottom,
             X_COLOR_BTNHIGHLIGHT);
        fill(dc, in.left, in.top, in.right, in.top + cyb,
             X_COLOR_BTNHIGHLIGHT);
        in.bottom -= cyb;
        in.right -= cxb;
        for (k = 0; k < 2; k++) {
            fill(dc, in.left, in.bottom, in.right + cxb, in.bottom + cyb,
                 COLOR_BTNSHADOW);
            fill(dc, in.right, in.top, in.right + cxb, in.bottom,
                 COLOR_BTNSHADOW);
            if (k == 0) {
                InflateRect(&in, -cxb, -cyb);
            }
        }
    }
    fill(dc, in.left, in.top, in.right, in.bottom, COLOR_BTNFACE);
}

static void draw_combo(LDLG *d, HWND w)
{
    int cxb = GetSystemMetrics(SM_CXBORDER);
    int cyb = GetSystemMetrics(SM_CYBORDER);
    int focus = GetFocus() == w, on = IsWindowEnabled(w), eh, n = 0;
    HFONT font = (HFONT) SendMessageW(w, WM_GETFONT, 0, 0);
    WCHAR text[AUTH_NAME_MAX + 2];
    RECT cr, er, br, rc;
    LRESULT cur, len;
    BITMAP bm;
    HBRUSH brush;
    HDC bdc;
    HGDIOBJ ob, obr, of;

    GetClientRect(w, &cr);
    eh = (int) SendMessageW(w, X_CB_GETITEMHEIGHT, (WPARAM) -1, 0);
    if (eh <= 0 || eh > cr.bottom) {
        eh = cr.bottom;
    }
    SetRect(&br, cr.right - GetSystemMetrics(SM_CXVSCROLL), 0, cr.right, eh);
    SetRect(&er, 0, 0, br.left + cxb > 0 ? br.left + cxb : 0, eh);
    combo_button(d->mdc, &br);
    if (d->arrow == NULL) {
        d->arrow = LoadBitmapA(NULL, MAKEINTRESOURCE(X_OBM_COMBO));
    }
    bdc = d->arrow != NULL && GetObjectA(d->arrow, sizeof(bm), &bm) != 0
          ? CreateCompatibleDC(d->mdc) : NULL;
    brush = bdc != NULL ? CreateSolidBrush(GetSysColor(on ? COLOR_BTNTEXT
                                                       : COLOR_GRAYTEXT))
            : NULL;
    if (brush != NULL) {
        ob = SelectObject(bdc, d->arrow);
        obr = SelectObject(d->mdc, brush);
        SetTextColor(d->mdc, RGB(0, 0, 0));
        SetBkColor(d->mdc, RGB(255, 255, 255));
        BitBlt(d->mdc, br.left + (br.right - br.left - bm.bmWidth) / 2,
               br.top + (br.bottom - br.top - bm.bmHeight) / 2, bm.bmWidth,
               bm.bmHeight, bdc, 0, 0, X_ROP_ARROW);
        SelectObject(d->mdc, obr);
        SelectObject(bdc, ob);
        DeleteObject(brush);
    }
    if (bdc != NULL) {
        DeleteDC(bdc);
    }
    frame(d->mdc, &er, cxb, cyb, COLOR_WINDOWFRAME);
    rc = er;
    InflateRect(&rc, -cxb, -cyb);
    fill(d->mdc, rc.left, rc.top, rc.right, rc.bottom, COLOR_WINDOW);
    InflateRect(&rc, -1, -1);
    cur = SendMessageW(w, CB_GETCURSEL, 0, 0);
    len = cur != CB_ERR ? SendMessageW(w, CB_GETLBTEXTLEN, (WPARAM) cur, 0)
          : CB_ERR;
    if (len >= 0 && len <= AUTH_NAME_MAX &&
        SendMessageW(w, CB_GETLBTEXT, (WPARAM) cur, (LPARAM) text) >= 0) {
        n = (int) len;
    }
    of = SelectObject(d->mdc, font != NULL ? font
                              : GetStockObject(SYSTEM_FONT));
    SetBkMode(d->mdc, OPAQUE);
    SetTextColor(d->mdc, GetSysColor(!on ? COLOR_GRAYTEXT : focus
                                     ? COLOR_HIGHLIGHTTEXT
                                     : COLOR_WINDOWTEXT));
    SetBkColor(d->mdc, GetSysColor(focus ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    ExtTextOutW(d->mdc, rc.left + 1, rc.top + 1, ETO_CLIPPED | ETO_OPAQUE,
                &rc, text, (UINT) n, NULL);
    if (focus) {
        DrawFocusRect(d->mdc, &rc);
    }
    SelectObject(d->mdc, of);
}

static int self_draw(LDLG *d, HWND w)
{
    static const SELST none = { 0, 0, 0 };

    if (is_class(w, "Edit")) {
        draw_edit(d, w, w == GetDlgItem(d->dlg, d->id_user) ? &d->sel[0]
                  : w == GetDlgItem(d->dlg, d->id_pw) ? &d->sel[1] : &none);
        return 1;
    }
    if (is_class(w, "ComboBox")) {
        draw_combo(d, w);
        return 1;
    }
    return 0;
}

static void client(LDLG *d, HWND w, const RECT *area, int erase)
{
    POINT p;
    RECT cr, lr;
    HDC bdc;
    HGDIOBJ old;
    BITMAP bm;
    int s;

    p.x = 0;
    p.y = 0;
    ClientToScreen(w, &p);
    GetClientRect(w, &cr);
    s = SaveDC(d->mdc);
    SetWindowOrgEx(d->mdc, area->left - p.x, area->top - p.y, NULL);
    IntersectClipRect(d->mdc, 0, 0, cr.right, cr.bottom);
    if (erase) {
        SendMessageW(w, WM_ERASEBKGND, (WPARAM) d->mdc, 0);
        if (w == d->dlg && d->brand != NULL) {
            paint_brand(d, (int) cr.right);
        }
    } else if (!d->self || !self_draw(d, w)) {
        SendMessageW(w, WM_PAINT, (WPARAM) d->mdc, 0);
    }
    if (!erase && w == d->dlg && d->logo != NULL &&
        GetDlgItem(w, I3X_LOGO) != NULL &&
        GetObjectA(d->logo, sizeof(bm), &bm) != 0) {
        GetWindowRect(GetDlgItem(w, I3X_LOGO), &lr);
        ScreenToClient(w, (POINT *) &lr.left);
        ScreenToClient(w, (POINT *) &lr.right);
        bdc = CreateCompatibleDC(d->mdc);
        if (bdc != NULL) {
            old = SelectObject(bdc, d->logo);
            BitBlt(d->mdc, lr.left + (lr.right - lr.left - bm.bmWidth) / 2,
                   lr.top + (lr.bottom - lr.top - bm.bmHeight) / 2,
                   bm.bmWidth, bm.bmHeight, bdc, 0, 0, SRCCOPY);
            SelectObject(bdc, old);
            DeleteDC(bdc);
        }
    }
    RestoreDC(d->mdc, s);
}

static void children(LDLG *d, HWND w, const RECT *area)
{
    HWND c[KIDS_MAX], k;
    RECT r;
    int n = 0, i, j, s, s2;

    for (k = GetWindow(w, GW_CHILD); k != NULL && n < KIDS_MAX;
         k = GetWindow(k, GW_HWNDNEXT)) {
        c[n++] = k;
    }
    for (i = 0; i < n; i++) {
        if (!shown(c[i])) {
            continue;
        }
        s = SaveDC(d->mdc);
        if (GetWindowLongA(c[i], GWL_STYLE) & WS_CLIPSIBLINGS) {
            for (j = 0; j < i; j++) {
                if (shown(c[j])) {
                    GetWindowRect(c[j], &r);
                    ExcludeClipRect(d->mdc, r.left - area->left,
                                    r.top - area->top, r.right - area->left,
                                    r.bottom - area->top);
                }
            }
        }
        GetWindowRect(c[i], &r);
        s2 = SaveDC(d->mdc);
        SetWindowOrgEx(d->mdc, area->left - r.left, area->top - r.top, NULL);
        if (d->look != LOOK_3X) {
            SendMessageW(c[i], X_WM_PRINT, (WPARAM) d->mdc, X_PRF_NONCLIENT);
        } else if (GetWindowLongA(c[i], GWL_STYLE) & WS_BORDER) {
            OffsetRect(&r, -r.left, -r.top);
            frame(d->mdc, &r, GetSystemMetrics(SM_CXBORDER),
                  GetSystemMetrics(SM_CYBORDER), COLOR_WINDOWFRAME);
        }
        RestoreDC(d->mdc, s2);
        client(d, c[i], area, 0);
        children(d, c[i], area);
        RestoreDC(d->mdc, s);
    }
}

static void draw_top(LDLG *d, HWND w, int active, const RECT *area)
{
    RECT r;
    int s;

    GetWindowRect(w, &r);
    s = SaveDC(d->mdc);
    SetWindowOrgEx(d->mdc, area->left - r.left, area->top - r.top, NULL);
    if (d->look != LOOK_3X) {
        if ((GetWindowLongA(w, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) {
            SendMessageW(w, WM_NCACTIVATE, (WPARAM) active, 0);
        }
        SendMessageW(w, X_WM_PRINT, (WPARAM) d->mdc, X_PRF_NONCLIENT);
    } else {
        frame3x(d, d->mdc, w, &r, active);
    }
    RestoreDC(d->mdc, s);
    client(d, w, area, 1);
    client(d, w, area, 0);
    children(d, w, area);
}

static int caret_phase(LDLG *d, DWORD now)
{
    HWND f = GetFocus();
    char cls[16];
    POINT p;
    UINT blink = GetCaretBlinkTime();

    if (!ours(d, f) || !IsWindowEnabled(f) ||
        !GetClassNameA(f, cls, sizeof(cls)) || lstrcmpiA(cls, "Edit") != 0 ||
        (d->self ? !shown(f) || !edit_caret(d, f, &p)
         : !IsWindowVisible(f) || !GetCaretPos(&p))) {
        d->caret_wnd = NULL;
        return -1;
    }
    if (f != d->caret_wnd || p.x != d->caret_at.x || p.y != d->caret_at.y) {
        d->caret_wnd = f;
        d->caret_at = p;
        d->caret_ms = now;
    }
    if (blink != 0 && blink != (UINT) -1 &&
        ((now - d->caret_ms) / blink) % 2 == 1) {
        return 0;
    }
    return 1;
}

static void caret_rect(LDLG *d, RECT *out)
{
    HWND f = d->caret_wnd;
    POINT p = d->caret_at;
    RECT cr;
    TEXTMETRICA tm, sys;
    HGDIOBJ old;
    HFONT font;

    font = (HFONT) SendMessageW(f, WM_GETFONT, 0, 0);
    old = SelectObject(d->mdc, GetStockObject(SYSTEM_FONT));
    GetTextMetricsA(d->mdc, &sys);
    if (font != NULL) {
        SelectObject(d->mdc, font);
    }
    GetTextMetricsA(d->mdc, &tm);
    SelectObject(d->mdc, old);
    GetClientRect(f, &cr);
    out->left = p.x;
    out->top = p.y;
    out->right = p.x + (sys.tmAveCharWidth <= tm.tmAveCharWidth ? 2 : 1);
    out->bottom = p.y + tm.tmHeight + (d->look == LOOK_3X ? 1 : 0);
    IntersectRect(out, out, &cr);
    ClientToScreen(f, (POINT *) &out->left);
    ClientToScreen(f, (POINT *) &out->right);
}

static void publish(LDLG *d, const RECT *area, int w, int h)
{
    BITMAPINFO bi;
    long stride = ((long) d->mw * 3 + 3) & ~3L;
    const unsigned char *src;
    unsigned char *dst;
    RECT ch;
    int y, x0, x1, n;

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = d->mw;
    bi.bmiHeader.biHeight = d->mh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    SelectObject(d->mdc, d->mold);
    n = GetDIBits(d->mdc, d->mbm, 0, (UINT) d->mh, d->rows, &bi,
                  DIB_RGB_COLORS);
    SelectObject(d->mdc, d->mbm);
    if (n != d->mh) {
        return;
    }
    SetRectEmpty(&ch);
    EnterCriticalSection(&d->lock);
    for (y = 0; y < h; y++) {
        src = d->rows + (long) (d->mh - 1 - y) * stride;
        dst = d->pic + (long) (area->top + y) * d->pic_stride +
              (long) area->left * 3;
        if (memcmp(dst, src, (size_t) w * 3) == 0) {
            continue;
        }
        for (x0 = 0; x0 < w && memcmp(dst + x0 * 3, src + x0 * 3, 3) == 0;
             x0++) {
        }
        for (x1 = w; x1 > x0 &&
             memcmp(dst + (x1 - 1) * 3, src + (x1 - 1) * 3, 3) == 0; x1--) {
        }
        memcpy(dst + x0 * 3, src + x0 * 3, (size_t) (x1 - x0) * 3);
        if (IsRectEmpty(&ch)) {
            SetRect(&ch, area->left + x0, area->top + y, area->left + x1,
                    area->top + y + 1);
        } else {
            if (area->left + x0 < ch.left) {
                ch.left = area->left + x0;
            }
            if (area->left + x1 > ch.right) {
                ch.right = area->left + x1;
            }
            ch.bottom = area->top + y + 1;
        }
    }
    if (!IsRectEmpty(&ch)) {
        join(&d->dirty, &ch);
        wake(d);
    }
    LeaveCriticalSection(&d->lock);
}

static void render(LDLG *d)
{
    HWND t[TOPS_MAX];
    HWND active = GetActiveWindow();
    RECT now, area, r, screen;
    HDC sdc;
    HBRUSH desk;
    int n = tops(d, t, TOPS_MAX), i, w, h;

    d->redraw = 0;
    d->drawn_ms = GetTickCount();
    if (d->self) {
        track(d);
    }
    SetRectEmpty(&now);
    for (i = 0; i < n; i++) {
        GetWindowRect(t[i], &r);
        join(&now, &r);
    }
    SetRect(&screen, 0, 0, (int) d->width, (int) d->height);
    IntersectRect(&now, &now, &screen);
    area = now;
    join(&area, &d->last);
    d->last = now;
    if (IsRectEmpty(&area)) {
        return;
    }
    w = area.right - area.left;
    h = area.bottom - area.top;
    if (d->mbm == NULL || w != d->mw || h != d->mh) {
        if (d->mbm != NULL) {
            SelectObject(d->mdc, d->mold);
            DeleteObject(d->mbm);
            d->mbm = NULL;
        }
        free(d->rows);
        d->rows = (unsigned char *) malloc((((size_t) w * 3 + 3) & ~3UL) *
                                           (size_t) h);
        sdc = GetDC(NULL);
        d->mbm = sdc != NULL ? CreateCompatibleBitmap(sdc, w, h) : NULL;
        if (sdc != NULL) {
            ReleaseDC(NULL, sdc);
        }
        if (d->mbm == NULL || d->rows == NULL) {
            if (d->mbm != NULL) {
                DeleteObject(d->mbm);
                d->mbm = NULL;
            }
            return;
        }
        d->mold = (HBITMAP) SelectObject(d->mdc, d->mbm);
        d->mw = w;
        d->mh = h;
    }
    SetWindowOrgEx(d->mdc, 0, 0, NULL);
    SetRect(&r, 0, 0, w, h);
    desk = CreateSolidBrush(GetSysColor(COLOR_BACKGROUND));
    if (desk != NULL) {
        FillRect(d->mdc, &r, desk);
        DeleteObject(desk);
    }
    for (i = n - 1; i >= 0; i--) {
        draw_top(d, t[i], t[i] == active, &area);
    }
    SetWindowOrgEx(d->mdc, 0, 0, NULL);
    d->phase = caret_phase(d, d->drawn_ms);
    if (d->phase == 1) {
        caret_rect(d, &r);
    }
    if (d->phase == 1 && IntersectRect(&r, &r, &area)) {
        PatBlt(d->mdc, r.left - area.left, r.top - area.top,
               r.right - r.left, r.bottom - r.top, DSTINVERT);
    }
    publish(d, &area, w, h);
}

static int blinked(LDLG *d, DWORD now)
{
    HWND f = d->caret_wnd;
    POINT p = d->caret_at;

    return caret_phase(d, now) != d->phase || f != d->caret_wnd ||
           p.x != d->caret_at.x || p.y != d->caret_at.y;
}

static int requests(LDLG *d)
{
    int end, fail;

    EnterCriticalSection(&d->lock);
    end = d->end;
    fail = d->fail;
    d->fail = 0;
    LeaveCriticalSection(&d->lock);
    if (end) {
        if (d->dlg != NULL && IsWindow(d->dlg)) {
            DestroyWindow(d->dlg);
        }
        d->dlg = NULL;
        PostQuitMessage(0);
        return 1;
    }
    if (fail) {
        show_fail(d);
    }
    return 0;
}

static LRESULT CALLBACK ctl_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
#ifdef _WIN64
    LDLG *d = (LDLG *) GetWindowLongPtrA(w, 0);
#else
    LDLG *d = (LDLG *) GetWindowLongA(w, 0);
#endif
    DWORD now;

    switch (m) {
    case WM_NCCREATE:
#ifdef _WIN64
        SetWindowLongPtrA(w, 0, (LONG_PTR) ((CREATESTRUCT *) lp)->lpCreateParams);
#else
        SetWindowLongA(w, 0, (LONG) ((CREATESTRUCT *) lp)->lpCreateParams);
#endif
        break;
    case WM_LD_NEXT:
    case WM_LD_WAKE:
    case WM_TIMER:
        if (d == NULL) {
            return 0;
        }
        if (m == WM_LD_NEXT) {
            d->busy = 0;
            if (d->self && d->dlg != NULL) {
                track(d);
            }
        }
        if (d->dlg == NULL || requests(d) || d->dlg == NULL) {
            return 0;
        }
        pump(d);
        now = GetTickCount();
        if (m == WM_TIMER && (d->redraw || now - d->drawn_ms >= REDRAW_MS ||
                              blinked(d, now))) {
            render(d);
        }
        return 0;
    }
    return DefWindowProcA(w, m, wp, lp);
}

static int find_dialog(LDLG *d)
{
    static const char *files[2] = { "msgina.dll", "winlogon.exe" };
    char path[MAX_PATH + 16];
    UINT n;
    int i;

    for (i = 0; i < 2; i++) {
        n = GetSystemDirectoryA(path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) {
            return 0;
        }
        strcat(path, "\\");
        strcat(path, files[i]);
        d->mod = LoadLibraryExA(path, NULL, DONT_RESOLVE_DLL_REFERENCES);
        if (d->mod == NULL) {
            continue;
        }
        if (FindResourceA(d->mod, MAKEINTRESOURCE(DLG_40), RT_DIALOG)) {
            d->look = LOOK_40;
            log_msg(LOG_INFO, "session %s: login: the logon dialog of %s "
                    "(NT 4.0)", d->peer, files[i]);
            return DLG_40;
        }
        if (FindResourceA(d->mod, MAKEINTRESOURCE(DLG_3X), RT_DIALOG)) {
            d->look = LOOK_3X;
            log_msg(LOG_INFO, "session %s: login: the logon dialog of %s "
                    "(NT 3.x)", d->peer, files[i]);
            return DLG_3X;
        }
        if (FindResourceA(d->mod, MAKEINTRESOURCE(DLG_50), RT_DIALOG)) {
            d->look = LOOK_50;
            log_msg(LOG_INFO, "session %s: login: the logon dialog of %s "
                    "(Windows 2000)", d->peer, files[i]);
            return DLG_50;
        }
        FreeLibrary(d->mod);
        d->mod = NULL;
    }
    log_msg(LOG_WARN, "session %s: login: no logon dialog in msgina.dll or "
            "winlogon.exe", d->peer);
    return 0;
}

typedef struct {
    HWND w;
    UINT flags;
} DESK_SWP;

static DWORD WINAPI desk_swp_thread(LPVOID arg)
{
    DESK_SWP *p = (DESK_SWP *) arg;

    SetWindowPos(p->w, NULL, 0, 0, 0, 0, p->flags | SWP_NOREDRAW |
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE);
    free(p);
    return 0;
}

static void desk_swp(HWND w, UINT flags)
{
    DESK_SWP *p = (DESK_SWP *) malloc(sizeof(DESK_SWP));
    HANDLE h = NULL;
    DWORD id;

    if (p != NULL) {
        p->w = w;
        p->flags = flags;
        h = CreateThread(NULL, 0, desk_swp_thread, p, 0, &id);
    }
    if (h == NULL) {
        free(p);
        return;
    }
    WaitForSingleObject(h, 5000);
    CloseHandle(h);
}

static int desk_shown(LDLG *d)
{
    UINT keep = SWP_NOREDRAW | SWP_NOACTIVATE | SWP_NOZORDER;
    HWND desk = GetDesktopWindow();
    HDC dc;
    RECT r;
    int kind = ERROR;

    SetRectEmpty(&r);
    if (!IsWindowVisible(desk)) {
        desk_swp(desk, SWP_SHOWWINDOW);
    }
    SetWindowPos(d->ctl, NULL, 0, 0, (int) d->width, (int) d->height,
                 keep | SWP_SHOWWINDOW);
    if ((dc = GetDC(d->ctl)) != NULL) {
        kind = GetClipBox(dc, &r);
        ReleaseDC(d->ctl, dc);
    }
    SetWindowPos(d->ctl, NULL, 0, 0, 1, 1, keep | SWP_HIDEWINDOW);
    if (kind != ERROR && IsRectEmpty(&r) && IsWindowVisible(desk)) {
        return 1;
    }
    if (IsWindowVisible(desk)) {
        desk_swp(desk, SWP_HIDEWINDOW);
    }
    log_msg(LOG_WARN, "session %s: login: our desktop is not ready for "
            "the logon dialog (%s; clip box %d: %d,%d - %d,%d)", d->peer,
            !IsRectEmpty(&r) ? "it would reach the screen"
            : kind == ERROR ? "no clip box" : "its window stays hidden",
            kind, (int) r.left, (int) r.top, (int) r.right,
            (int) r.bottom);
    return 0;
}

static int start(LDLG *d)
{
    WNDCLASSA wc;
    char name[16];
    unsigned char bg[3];
    void *t;
    int id = find_dialog(d);
    long i;
    HWND f;

    if (id == 0) {
        return 0;
    }
    if (!g_class) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = ctl_proc;
#ifdef _WIN64
        wc.cbWndExtra = sizeof(LONG_PTR);
#else
        wc.cbWndExtra = sizeof(LONG);
#endif
        wc.hInstance = GetModuleHandleA(NULL);
        wc.lpszClassName = CLASS_NAME;
        if (!RegisterClassA(&wc) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            log_msg(LOG_ERROR, "session %s: login: RegisterClass failed, "
                    "error %lu", d->peer, (unsigned long) GetLastError());
            return 0;
        }
        g_class = 1;
    }
    d->ctl = CreateWindowExA(0, CLASS_NAME, "", WS_POPUP, 0, 0, 1, 1, NULL,
                             NULL, GetModuleHandleA(NULL), d);
    t = copy_template(d, id);
    if (d->ctl == NULL || t == NULL ||
        SetTimer(d->ctl, TIMER_ID, TIMER_MS, NULL) == 0) {
        log_msg(LOG_ERROR, "session %s: login: no window or timer, error "
                "%lu", d->peer, (unsigned long) GetLastError());
        free(t);
        return 0;
    }
    if (d->look == LOOK_3X && (GetVersion() & 0xFFFF) == 0x0A03) {
        d->self = 1;
        log_msg(LOG_INFO, "session %s: login: NT 3.1: the desktop's "
                "window stays hidden, so ntrdps draws the edit and combo "
                "boxes of the dialog itself", d->peer);
    } else if (d->look == LOOK_3X && !desk_shown(d)) {
        free(t);
        return 0;
    }
    d->dlg = CreateDialogIndirectParamW(d->mod, (LPDLGTEMPLATEW) t, NULL,
                                        (DLGPROC) dlg_proc, (LPARAM) d);
    free(t);
    if (d->dlg == NULL) {
        log_msg(LOG_ERROR, "session %s: login: CreateDialogIndirectParam "
                "failed, error %lu", d->peer,
                (unsigned long) GetLastError());
        return 0;
    }
    if (!setup_dialog(d)) {
        return 0;
    }
    d->mdc = CreateCompatibleDC(NULL);
    d->pic_stride = (long) d->width * 3;
    d->pic = (unsigned char *) malloc((size_t) d->pic_stride * d->height);
    if (d->mdc == NULL || d->pic == NULL) {
        log_msg(LOG_ERROR, "session %s: login: no memory for the picture",
                d->peer);
        return 0;
    }
    ldlg_background(bg);
    for (i = 0; i < (long) d->width * (long) d->height; i++) {
        d->pic[i * 3] = bg[2];
        d->pic[i * 3 + 1] = bg[1];
        d->pic[i * 3 + 2] = bg[0];
    }
    d->km.ctx = d;
    d->km.emit = emit;
    d->km.map_vk = map_vk;
    d->km.sas = sas;
    d->km.win_keys = 1;
    keymap_init(&d->km);
    d->km.altgr_fake_ctrl = GetKeyboardLayoutNameA(name) &&
                            (strtoul(name, NULL, 16) & 0xFFFF) != 0x0409;
    center(d, d->dlg);
    ShowWindow(d->dlg, SW_SHOW);
    f = GetDlgItem(d->dlg, d->init_user[0] != 0 ? d->id_pw : d->id_user);
    SetFocus(f);
    d->phase = -1;
    render(d);
    return 1;
}

static void desk_name(char *name)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char r[8];
    int i, n = (int) strlen(DESK_NAME);

    tls_random(r, sizeof(r));
    memcpy(name, DESK_NAME, (size_t) n);
    name[n++] = '-';
    for (i = 0; i < 8; i++) {
        name[n++] = hex[r[i] >> 4];
        name[n++] = hex[r[i] & 15];
    }
    name[n] = 0;
}

static DWORD WINAPI ldlg_thread(LPVOID arg)
{
    LDLG *d = (LDLG *) arg;
    DESKTOP_THREAD t;
    MSG msg;
    char name[sizeof(DESK_NAME) + 20];

    memset(&t, 0, sizeof(t));
    t.who = "login";
    desk_name(name);
    if (!desktop_enter_own(&t, name) || !start(d)) {
        d->up = -1;
    } else {
        d->up = 1;
    }
    SetEvent(d->ready);
    while (d->up == 1 && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (d->dlg != NULL && IsWindow(d->dlg) &&
            IsDialogMessageW(d->dlg, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (d->dlg != NULL && IsWindow(d->dlg)) {
        DestroyWindow(d->dlg);
    }
    if (d->ctl != NULL) {
        KillTimer(d->ctl, TIMER_ID);
        DestroyWindow(d->ctl);
    }
    if (d->mdc != NULL) {
        if (d->mbm != NULL) {
            SelectObject(d->mdc, d->mold);
            DeleteObject(d->mbm);
        }
        DeleteDC(d->mdc);
    }
    if (d->logo != NULL) {
        DeleteObject(d->logo);
    }
    if (d->sysbox != NULL) {
        DeleteObject(d->sysbox);
    }
    if (d->arrow != NULL) {
        DeleteObject(d->arrow);
    }
    if (d->icon != NULL) {
        DestroyIcon(d->icon);
    }
    if (d->brand != NULL) {
        DeleteObject(d->brand);
    }
    if (d->band != NULL) {
        DeleteObject(d->band);
    }
    if (d->mod != NULL) {
        FreeLibrary(d->mod);
    }
    free(d->rows);
    memset(d->keys, 0, sizeof(d->keys));
    desktop_leave(&t);
    return 0;
}

static void free_ldlg(LDLG *d)
{
    CloseHandle(d->ready);
    DeleteCriticalSection(&d->lock);
    memset(d->pw, 0, sizeof(d->pw));
    free(d->pic);
    free(d);
}

LDLG *ldlg_open(unsigned int width, unsigned int height, const WCHAR *user,
                const WCHAR *domain, LDLG_WAKE wakefn, void *ctx,
                const char *peer)
{
    LDLG *d;
    DWORD r;

    if (width == 0 || height == 0 || width > 4096 || height > 4096) {
        return NULL;
    }
    d = (LDLG *) calloc(1, sizeof(LDLG));
    if (d == NULL) {
        return NULL;
    }
    d->width = width;
    d->height = height;
    wcopy(d->init_user, user, AUTH_NAME_MAX + 1);
    wcopy(d->init_domain, domain, AUTH_NAME_MAX + 1);
    strncpy(d->peer, peer, sizeof(d->peer) - 1);
    d->wake = wakefn;
    d->wake_ctx = ctx;
    InitializeCriticalSection(&d->lock);
    d->ready = CreateEvent(NULL, TRUE, FALSE, NULL);
    d->thread = d->ready != NULL
        ? CreateThread(NULL, 0, ldlg_thread, d, 0, &d->tid) : NULL;
    if (d->thread == NULL) {
        log_msg(LOG_ERROR, "session %s: login: CreateThread failed, error "
                "%lu", peer, (unsigned long) GetLastError());
        if (d->ready != NULL) {
            CloseHandle(d->ready);
        }
        DeleteCriticalSection(&d->lock);
        free(d);
        return NULL;
    }
    r = WaitForSingleObject(d->ready, OPEN_WAIT_MS);
    if (r == WAIT_OBJECT_0 && d->up == 1) {
        return d;
    }
    if (r != WAIT_OBJECT_0) {
        log_msg(LOG_WARN, "session %s: login: the logon dialog did not "
                "come within %d s", peer, OPEN_WAIT_MS / 1000);
    }
    ldlg_close(d);
    return NULL;
}

static void add(LDLG *d, const EVT *e)
{
    int was;

    EnterCriticalSection(&d->lock);
    was = d->q_count;
    if (d->q_count < EVT_MAX) {
        d->q[(d->q_first + d->q_count++) % EVT_MAX] = *e;
    } else if (!d->q_full) {
        d->q_full = 1;
        log_msg(LOG_WARN, "session %s: login: too much input at once: "
                "some is dropped", d->peer);
    }
    LeaveCriticalSection(&d->lock);
    if (was == 0) {
        PostMessageA(d->ctl, WM_LD_WAKE, 0, 0);
    }
}

void ldlg_key(LDLG *d, unsigned int code, unsigned int prefix, int down)
{
    EVT e;

    memset(&e, 0, sizeof(e));
    e.kind = EV_KEY;
    e.code = (unsigned char) code;
    e.prefix = (unsigned char) prefix;
    e.down = (unsigned char) (down != 0);
    add(d, &e);
}

void ldlg_locks(LDLG *d, unsigned int locks)
{
    EVT e;

    memset(&e, 0, sizeof(e));
    e.kind = EV_LOCKS;
    e.buttons = (unsigned short) locks;
    add(d, &e);
}

void ldlg_mouse(LDLG *d, int x, int y, unsigned int buttons)
{
    EVT e;

    memset(&e, 0, sizeof(e));
    e.kind = EV_MOUSE;
    e.x = (short) x;
    e.y = (short) y;
    e.buttons = (unsigned short) buttons;
    add(d, &e);
}

int ldlg_event(LDLG *d, WCHAR *user, WCHAR *domain, WCHAR *pw)
{
    int ev;

    EnterCriticalSection(&d->lock);
    ev = d->event;
    d->event = LDLG_NONE;
    if (ev == LDLG_SUBMIT) {
        wcopy(user, d->user, LDLG_USER_MAX + 1);
        wcopy(domain, d->domain, AUTH_NAME_MAX + 1);
        wcopy(pw, d->pw, AUTH_PW_MAX + 1);
        memset(d->pw, 0, sizeof(d->pw));
    }
    LeaveCriticalSection(&d->lock);
    if (ev == LDLG_NONE &&
        WaitForSingleObject(d->thread, 0) == WAIT_OBJECT_0) {
        ev = LDLG_FAILED;
    }
    return ev;
}

void ldlg_fail(LDLG *d)
{
    EnterCriticalSection(&d->lock);
    d->fail = 1;
    LeaveCriticalSection(&d->lock);
    PostMessageA(d->ctl, WM_LD_WAKE, 0, 0);
}

int ldlg_picture(LDLG *d, LDLG_DRAW draw, void *ctx)
{
    RECT r;

    EnterCriticalSection(&d->lock);
    r = d->dirty;
    SetRectEmpty(&d->dirty);
    if (!IsRectEmpty(&r)) {
        draw(ctx, r.left, r.top, r.right - r.left, r.bottom - r.top,
             d->pic + (long) r.top * d->pic_stride + (long) r.left * 3,
             d->pic_stride);
    }
    LeaveCriticalSection(&d->lock);
    return !IsRectEmpty(&r);
}

void ldlg_background(unsigned char *rgb)
{
    COLORREF c = GetSysColor(COLOR_BACKGROUND);

    rgb[0] = GetRValue(c);
    rgb[1] = GetGValue(c);
    rgb[2] = GetBValue(c);
}

void ldlg_close(LDLG *d)
{
    if (d == NULL) {
        return;
    }
    EnterCriticalSection(&d->lock);
    d->end = 1;
    d->wake = NULL;
    LeaveCriticalSection(&d->lock);
    if (d->ctl != NULL) {
        PostMessageA(d->ctl, WM_LD_WAKE, 0, 0);
    }
    if (WaitForSingleObject(d->thread, END_WAIT_MS) != WAIT_OBJECT_0) {
        log_msg(LOG_WARN, "session %s: login: the logon dialog's thread did "
                "not end within %d s", d->peer, END_WAIT_MS / 1000);
        CloseHandle(d->thread);
        return;
    }
    CloseHandle(d->thread);
    free_ldlg(d);
}
