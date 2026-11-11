/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "session.h"
#include "auth.h"
#include "sec.h"
#include "share.h"
#include "update.h"
#include "rdp.h"
#include "lfont.h"
#include "ldlg.h"
#include "login.h"

#define BOX_W    460
#define EDGE     3
#define TITLE_H  22
#define MARGIN   16
#define FIELD_X  (MARGIN + 12 * LFONT_W)
#define FIELD_W  (BOX_W - FIELD_X - MARGIN)
#define FIELD_H  (LFONT_H + 8)
#define FIELD_IN 4
#define BTN_W    90
#define BTN_H    (LFONT_H + 12)
#define ROW_INFO (EDGE + TITLE_H + 14)
#define ROW_USER (ROW_INFO + 34)
#define ROW_PW   (ROW_USER + 32)
#define ROW_MSG  (ROW_PW + 36)

#define F_USER 0
#define F_PW   1

#define C_DESK   0
#define C_FACE   1
#define C_WHITE  2
#define C_SHADOW 3
#define C_BLACK  4
#define C_TITLE  5
#define C_RED    6
#define COLORS   7

static const unsigned char rgb[COLORS][3] = {
    { 0, 128, 128 },
    { 192, 192, 192 },
    { 255, 255, 255 },
    { 128, 128, 128 },
    { 0, 0, 0 },
    { 0, 0, 128 },
    { 128, 0, 0 }
};

#define M_LSHIFT 0x01
#define M_RSHIFT 0x02
#define M_LCTRL  0x04
#define M_RCTRL  0x08
#define M_LALT   0x10
#define M_RALT   0x20
#define M_SHIFT  (M_LSHIFT | M_RSHIFT)
#define M_CTRL   (M_LCTRL | M_RCTRL)

typedef struct LOGIN_s {
    SESSION        *s;
    const CORE_API *real;
    void           *real_ctx;
    CORE_FRAME      f;
    unsigned char  *bits;
    unsigned char   pal[256][3];
    int             pb;
    unsigned long   col[COLORS];
    int             x0, y0;
    int             h;
    char            title[48];
    WCHAR           text[2][AUTH_PW_MAX + 1];
    unsigned int    len[2];
    int             focus;
    char            msg[64];
    int             msg_color;
    int             tries;
    DWORD           start_ms;
    int             check_due;
    DWORD           check_ms;
    unsigned int    locks;
    unsigned int    mods;
    int             enter_down;
    unsigned int    buttons;
    int             press;
    LDLG           *dlg;
    WCHAR           domain[AUTH_NAME_MAX + 1];
    unsigned long   ckey[256];
    unsigned long   cval[256];
} LOGIN;

static const unsigned int field_max[2] = { 2 * AUTH_NAME_MAX + 1,
                                           AUTH_PW_MAX };

static unsigned long scale(unsigned int v, unsigned long mask)
{
    unsigned int shift = 0, bits = 0;

    if (mask == 0) {
        return 0;
    }
    while (!(mask & 1)) {
        mask >>= 1;
        shift++;
    }
    while (mask & 1) {
        mask >>= 1;
        bits++;
    }
    if (bits > 8) {
        bits = 8;
    }
    return (unsigned long) (v >> (8 - bits)) << shift;
}

static unsigned long pixel(const LOGIN *L, const unsigned char *c)
{
    unsigned long d, best = 0, bd = 0xFFFFFFFFUL;
    long dr, dg, db;
    unsigned int i;

    switch (L->f.bpp) {
    case 8:
        for (i = 0; i < 256; i++) {
            dr = (long) L->pal[i][0] - c[0];
            dg = (long) L->pal[i][1] - c[1];
            db = (long) L->pal[i][2] - c[2];
            d = (unsigned long) (dr * dr + dg * dg + db * db);
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
        return best;
    case 16:
        return scale(c[0], L->f.red_mask) | scale(c[1], L->f.green_mask) |
               scale(c[2], L->f.blue_mask);
    }
    return (unsigned long) c[2] | ((unsigned long) c[1] << 8) |
           ((unsigned long) c[0] << 16);
}

static void fill_px(LOGIN *L, int x0, int y0, int x1, int y1,
                    unsigned long v)
{
    unsigned char *p;
    int x, y, b = L->pb;

    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > (int) L->f.width) {
        x1 = (int) L->f.width;
    }
    if (y1 > (int) L->f.height) {
        y1 = (int) L->f.height;
    }
    for (y = y0; y < y1; y++) {
        p = L->bits + (long) y * L->f.stride + (long) x0 * b;
        for (x = x0; x < x1; x++, p += b) {
            p[0] = (unsigned char) v;
            if (b > 1) {
                p[1] = (unsigned char) (v >> 8);
            }
            if (b > 2) {
                p[2] = (unsigned char) (v >> 16);
            }
            if (b > 3) {
                p[3] = 0;
            }
        }
    }
}

static void fill(LOGIN *L, int x0, int y0, int x1, int y1, int color)
{
    fill_px(L, x0, y0, x1, y1, L->col[color]);
}

static void glyph(LOGIN *L, int x, int y, unsigned int ch, int color)
{
    const unsigned short *g;
    int r, c;

    if (ch < LFONT_FIRST || ch >= LFONT_FIRST + LFONT_CHARS) {
        ch = '?';
    }
    g = lfont_bits[ch - LFONT_FIRST];
    for (r = 0; r < LFONT_H; r++) {
        for (c = 0; c < LFONT_W; c++) {
            if (g[r] & (0x8000U >> c)) {
                fill(L, x + c, y + r, x + c + 1, y + r + 1, color);
            }
        }
    }
}

static void text(LOGIN *L, int x, int y, const char *t, int color, int room)
{
    for (; *t != '\0' && room >= LFONT_W; t++) {
        glyph(L, x, y, (unsigned char) *t, color);
        x += LFONT_W;
        room -= LFONT_W;
    }
}

static void damage(LOGIN *L, int x0, int y0, int x1, int y1)
{
    update_area(L->s, x0, y0, x1, y1);
}

static void edge(LOGIN *L, int x, int y, int w, int h, int in)
{
    int tl_out = in ? C_SHADOW : C_FACE, tl_in = in ? C_BLACK : C_WHITE;
    int br_out = in ? C_WHITE : C_BLACK, br_in = in ? C_FACE : C_SHADOW;

    fill(L, x, y, x + w, y + 1, tl_out);
    fill(L, x, y, x + 1, y + h, tl_out);
    fill(L, x, y + h - 1, x + w, y + h, br_out);
    fill(L, x + w - 1, y, x + w, y + h, br_out);
    fill(L, x + 1, y + 1, x + w - 1, y + 2, tl_in);
    fill(L, x + 1, y + 1, x + 2, y + h - 1, tl_in);
    fill(L, x + 1, y + h - 2, x + w - 1, y + h - 1, br_in);
    fill(L, x + w - 2, y + 1, x + w - 1, y + h - 1, br_in);
}

static int row_last(const LOGIN *L)
{
    return ROW_MSG + 34;
}

static int field_y(int i)
{
    return (i == F_USER ? ROW_USER : ROW_PW) - FIELD_IN;
}

static void draw_field(LOGIN *L, int i)
{
    int x = L->x0 + FIELD_X, y = L->y0 + field_y(i);
    unsigned int vis = (FIELD_W - 2 * FIELD_IN) / LFONT_W - 1;
    unsigned int first, k;
    int cx;

    edge(L, x, y, FIELD_W, FIELD_H, 1);
    fill(L, x + 2, y + 2, x + FIELD_W - 2, y + FIELD_H - 2, C_WHITE);
    first = L->len[i] > vis ? L->len[i] - vis : 0;
    for (k = first; k < L->len[i]; k++) {
        glyph(L, x + FIELD_IN + (int) (k - first) * LFONT_W, y + FIELD_IN,
              i == F_PW ? '*' : L->text[i][k], C_BLACK);
    }
    if (L->focus == i && !L->check_due) {
        cx = x + FIELD_IN + (int) (L->len[i] - first) * LFONT_W;
        fill(L, cx, y + 3, cx + 2, y + FIELD_H - 3, C_BLACK);
    }
    damage(L, x, y, x + FIELD_W, y + FIELD_H);
}

static void draw_msg(LOGIN *L)
{
    int x = L->x0 + MARGIN, y = L->y0 + ROW_MSG;

    fill(L, x, y, L->x0 + BOX_W - MARGIN, y + LFONT_H, C_FACE);
    text(L, x, y, L->msg, L->msg_color, BOX_W - 2 * MARGIN);
    damage(L, x, y, L->x0 + BOX_W - MARGIN, y + LFONT_H);
}

static void set_msg(LOGIN *L, const char *m, int color)
{
    strncpy(L->msg, m, sizeof(L->msg) - 1);
    L->msg[sizeof(L->msg) - 1] = '\0';
    L->msg_color = color;
    draw_msg(L);
}

static void button_rect(const LOGIN *L, RECT *r)
{
    r->left = L->x0 + BOX_W - MARGIN - BTN_W;
    r->top = L->y0 + row_last(L) - 6;
    r->right = r->left + BTN_W;
    r->bottom = r->top + BTN_H;
}

static void draw_all(LOGIN *L)
{
    int x = L->x0, y = L->y0, w = BOX_W, h = L->h;
    RECT b;

    fill(L, 0, 0, (int) L->f.width, (int) L->f.height, C_DESK);
    fill(L, x, y, x + w, y + h, C_FACE);
    edge(L, x, y, w, h, 0);
    fill(L, x + EDGE, y + EDGE, x + w - EDGE, y + EDGE + TITLE_H, C_TITLE);
    text(L, x + EDGE + 6, y + EDGE + (TITLE_H - LFONT_H) / 2, L->title,
         C_WHITE, w - 2 * EDGE - 12);
    text(L, x + MARGIN, y + ROW_INFO, auth_windows()
         ? "Log on with an administrator account."
         : "Log on with the ntrdps account.", C_BLACK, w - 2 * MARGIN);
    text(L, x + MARGIN, y + ROW_USER, "User name:", C_BLACK, FIELD_X);
    text(L, x + MARGIN, y + ROW_PW, "Password:", C_BLACK, FIELD_X);
    draw_field(L, F_USER);
    draw_field(L, F_PW);
    draw_msg(L);
    text(L, x + MARGIN, y + row_last(L), "Enter: log on   Tab: next field",
         C_BLACK, w - 2 * MARGIN - BTN_W);
    button_rect(L, &b);
    fill(L, b.left, b.top, b.right, b.bottom, C_BLACK);
    fill(L, b.left + 1, b.top + 1, b.right - 1, b.bottom - 1, C_FACE);
    edge(L, b.left + 1, b.top + 1, BTN_W - 2, BTN_H - 2, 0);
    text(L, b.left + (BTN_W - 6 * LFONT_W) / 2,
         b.top + (BTN_H - LFONT_H) / 2, "Log on", C_BLACK, BTN_W);
    damage(L, 0, 0, (int) L->f.width, (int) L->f.height);
}

static void set_focus(LOGIN *L, int i)
{
    int old = L->focus;

    L->focus = i;
    draw_field(L, old);
    draw_field(L, i);
}

static void add_char(LOGIN *L, WCHAR c)
{
    int i = L->focus;

    if (L->len[i] < field_max[i]) {
        L->text[i][L->len[i]++] = c;
        L->text[i][L->len[i]] = 0;
        draw_field(L, i);
    }
}

static void clear_field(LOGIN *L, int i)
{
    memset(L->text[i], 0, sizeof(L->text[i]));
    L->len[i] = 0;
    draw_field(L, i);
}

static void submit(LOGIN *L)
{
    if (L->len[F_USER] == 0) {
        set_msg(L, "Type a user name.", C_RED);
        set_focus(L, F_USER);
        return;
    }
    L->check_due = 1;
    L->check_ms = GetTickCount();
    set_msg(L, "Checking ...", C_BLACK);
    draw_field(L, L->focus);
}

static void enter(LOGIN *L)
{
    if (L->focus == F_USER && L->len[F_PW] == 0 && L->len[F_USER] > 0) {
        set_focus(L, F_PW);
        return;
    }
    submit(L);
}

static unsigned int mod_bit(unsigned int sc, unsigned int prefix)
{
    if (prefix == 0) {
        switch (sc) {
        case 0x2A: return M_LSHIFT;
        case 0x36: return M_RSHIFT;
        case 0x1D: return M_LCTRL;
        case 0x38: return M_LALT;
        }
    } else if (prefix == 0xE0) {
        switch (sc) {
        case 0x1D: return M_RCTRL;
        case 0x38: return M_RALT;
        }
    }
    return 0;
}

static void type_key(LOGIN *L, unsigned int sc)
{
    BYTE ks[256];
    WCHAR out[8];
    UINT vk;
    int n, i, ctrl = (L->mods & M_CTRL) != 0;
    int alt = (L->mods & (M_LALT | M_RALT)) != 0;

    if ((ctrl && !alt) || ((L->mods & M_LALT) && !ctrl)) {
        return;
    }
    vk = MapVirtualKeyA(sc, 1);
    if (vk == 0) {
        return;
    }
    memset(ks, 0, sizeof(ks));
    if (L->mods & M_SHIFT) {
        ks[VK_SHIFT] = 0x80;
    }
    if (L->locks & CORE_LOCK_CAPS) {
        ks[VK_CAPITAL] = 0x01;
    }
    if (alt) {
        ks[VK_CONTROL] = 0x80;
        ks[VK_MENU] = 0x80;
    }
    n = ToUnicode(vk, sc, ks, out, sizeof(out) / sizeof(out[0]), 0);
    for (i = 0; i < n; i++) {
        if (out[i] >= 0x20 && out[i] != 0x7F) {
            add_char(L, out[i]);
        }
    }
}

static void dlg_key(LOGIN *L, unsigned int sc, unsigned int prefix,
                    int down)
{
    unsigned int p;

    if (prefix == 0 && down && (sc == 0x3A || sc == 0x45 || sc == 0x46)) {
        L->locks ^= sc == 0x3A ? CORE_LOCK_CAPS
                  : sc == 0x45 ? CORE_LOCK_NUM : CORE_LOCK_SCROLL;
    }
    if (sc == 0x1C && prefix != 0xE1) {
        if (down) {
            L->enter_down = prefix == 0xE0 ? 2 : 1;
        } else if (L->enter_down) {
            p = L->enter_down == 2 ? 0xE0 : 0;
            L->enter_down = 0;
            ldlg_key(L->dlg, sc, p, 1);
            ldlg_key(L->dlg, sc, p, 0);
        }
        return;
    }
    ldlg_key(L->dlg, sc, prefix, down);
}

static void __cdecl l_key(void *core, void *session, unsigned int sc,
                          unsigned int prefix, int down)
{
    static const char pad[] = "789-456+1230.";
    LOGIN *L = (LOGIN *) core;
    unsigned int m = mod_bit(sc, prefix);

    if (L->dlg != NULL) {
        dlg_key(L, sc, prefix, down);
        return;
    }
    if (m != 0) {
        if (down) {
            L->mods |= m;
        } else {
            L->mods &= ~m;
        }
        return;
    }
    if (prefix == 0 && down && (sc == 0x3A || sc == 0x45 || sc == 0x46)) {
        L->locks ^= sc == 0x3A ? CORE_LOCK_CAPS
                  : sc == 0x45 ? CORE_LOCK_NUM : CORE_LOCK_SCROLL;
        return;
    }
    if (L->check_due || L->s->ending != END_NO) {
        return;
    }
    if (sc == 0x1C && prefix != 0xE1) {
        if (down) {
            L->enter_down = 1;
        } else if (L->enter_down) {
            L->enter_down = 0;
            enter(L);
        }
        return;
    }
    if (!down) {
        return;
    }
    if (prefix == 0xE0) {
        if (sc == 0x48 || sc == 0x50) {
            set_focus(L, sc == 0x48 ? F_USER : F_PW);
        } else if (sc == 0x35) {
            add_char(L, '/');
        }
        return;
    }
    if (prefix != 0) {
        return;
    }
    switch (sc) {
    case 0x0F:
        set_focus(L, L->focus == F_USER ? F_PW : F_USER);
        return;
    case 0x0E:
        if (L->len[L->focus] > 0) {
            L->text[L->focus][--L->len[L->focus]] = 0;
            draw_field(L, L->focus);
        }
        return;
    case 0x01:
        clear_field(L, L->focus);
        return;
    }
    if (sc >= 0x47 && sc <= 0x53) {
        if (sc == 0x4A || sc == 0x4E ||
            ((L->locks & CORE_LOCK_NUM) && !(L->mods & M_SHIFT))) {
            add_char(L, (WCHAR) pad[sc - 0x47]);
        }
        return;
    }
    type_key(L, sc);
}

static void __cdecl l_key_locks(void *core, void *session,
                                unsigned int locks)
{
    LOGIN *L = (LOGIN *) core;

    L->locks = locks;
    if (L->dlg != NULL) {
        ldlg_locks(L->dlg, locks);
    }
}

static int inside(int x, int y, int left, int top, int w, int h)
{
    return x >= left && x < left + w && y >= top && y < top + h;
}

static void __cdecl l_mouse(void *core, void *session, int x, int y,
                            unsigned int buttons, int wheel)
{
    LOGIN *L = (LOGIN *) core;
    int was = (L->buttons & CORE_BUTTON_LEFT) != 0;
    int now = (buttons & CORE_BUTTON_LEFT) != 0;
    RECT b;
    int i;

    if (L->dlg != NULL) {
        ldlg_mouse(L->dlg, x, y, buttons);
        return;
    }
    L->buttons = buttons;
    if (L->check_due || L->s->ending != END_NO || was == now) {
        return;
    }
    button_rect(L, &b);
    if (now) {
        for (i = F_USER; i <= F_PW; i++) {
            if (inside(x, y, L->x0 + FIELD_X, L->y0 + field_y(i), FIELD_W,
                       FIELD_H)) {
                set_focus(L, i);
            }
        }
        L->press = inside(x, y, b.left, b.top, BTN_W, BTN_H);
        return;
    }
    if (L->press && inside(x, y, b.left, b.top, BTN_W, BTN_H)) {
        submit(L);
    }
    L->press = 0;
}

static void *__cdecl l_open(void *core, const FRONTEND_API *fe,
                            void *fe_ctx, const char *peer,
                            unsigned int flags)
{
    return NULL;
}

static void __cdecl l_close(void *core, void *session)
{
}

static void __cdecl l_view_only(void *core, void *session, int on)
{
}

static int __cdecl l_frame_lock(void *core, CORE_FRAME *f)
{
    unsigned long cb = f->cb;

    memcpy(f, &((LOGIN *) core)->f,
           cb < sizeof(CORE_FRAME) ? cb : sizeof(CORE_FRAME));
    f->cb = cb;
    return 1;
}

static void __cdecl l_frame_unlock(void *core)
{
}

static int __cdecl l_pointer_get(void *core, CORE_POINTER *p)
{
    p->serial = 0;
    return 1;
}

static void __cdecl l_sas(void *core, void *session)
{
}

static void __cdecl l_log(void *core, int level, const char *msg)
{
    log_msg(level, "%s", msg);
}

static int __cdecl l_ready(void *core, void *session)
{
    return 0;
}

static int __cdecl l_orders_get(void *core, unsigned long *next,
                                CORE_ORDER *out, int max)
{
    return 0;
}

static int __cdecl l_text_get(void *core, const CORE_ORDER *o,
                              CORE_GLYPHREF *refs)
{
    return 0;
}

static const CORE_GLYPH *__cdecl l_glyph_get(void *core, unsigned int id,
                                             unsigned long serial)
{
    return NULL;
}

static const CORE_BITMAP *__cdecl l_bitmap_get(void *core, unsigned int id,
                                               unsigned long serial)
{
    return NULL;
}

static const CORE_API login_api = {
    sizeof(CORE_API), CORE_VERSION,
    l_open, l_close, l_view_only,
    l_frame_lock, l_frame_unlock,
    l_pointer_get,
    l_key, l_key_locks, l_mouse, l_sas,
    l_log,
    l_ready,
    l_orders_get,
    l_text_get, l_glyph_get,
    l_bitmap_get
};

void login_free(SESSION *s)
{
    LOGIN *L = s->login;

    if (L == NULL) {
        return;
    }
    if (L->dlg != NULL) {
        ldlg_close(L->dlg);
        L->dlg = NULL;
    }
    s->core = L->real;
    s->core_ctx = L->real_ctx;
    s->login = NULL;
    memset(L->text, 0, sizeof(L->text));
    free(L->bits);
    free(L);
}

static void put(void *ctx, int x, int y, int w, int h,
                const unsigned char *bgr, long stride)
{
    LOGIN *L = (LOGIN *) ctx;
    const unsigned char *src;
    unsigned char c[3], *p;
    unsigned long key, v;
    unsigned int k;
    int i, j, b = L->pb;

    for (j = 0; j < h; j++) {
        src = bgr + (long) j * stride;
        p = L->bits + (long) (y + j) * L->f.stride + (long) x * b;
        for (i = 0; i < w; i++, src += 3, p += b) {
            key = 0x1000000UL | ((unsigned long) src[2] << 16) |
                  ((unsigned long) src[1] << 8) | src[0];
            k = (src[0] + src[1] * 3U + src[2] * 7U) & 255;
            if (L->ckey[k] != key) {
                c[0] = src[2];
                c[1] = src[1];
                c[2] = src[0];
                L->ckey[k] = key;
                L->cval[k] = pixel(L, c);
            }
            v = L->cval[k];
            p[0] = (unsigned char) v;
            if (b > 1) {
                p[1] = (unsigned char) (v >> 8);
            }
            if (b > 2) {
                p[2] = (unsigned char) (v >> 16);
            }
            if (b > 3) {
                p[3] = 0;
            }
        }
    }
    damage(L, x, y, x + w, y + h);
}

static void wake(void *ctx)
{
    session_wake((SESSION *) ctx);
}

static int make_box(SESSION *s, const char *msg)
{
    LOGIN *L;
    CORE_FRAME f;
    char name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD n = sizeof(name);
    int i;

    memset(&f, 0, sizeof(f));
    f.cb = sizeof(f);
    if (!s->core->FrameLock(s->core_ctx, &f)) {
        log_msg(LOG_ERROR, "session %s: login: the core has no screen",
                s->peer);
        return 0;
    }
    L = (LOGIN *) calloc(1, sizeof(LOGIN));
    if (L != NULL) {
        L->f = f;
        L->pb = f.bpp == 32 ? 4 : (int) f.bpp / 8;
        L->f.stride = ((long) f.width * L->pb + 3) & ~3L;
        L->bits = (unsigned char *) malloc((size_t) L->f.stride * f.height);
        if (f.bpp == 8 && f.palette != NULL) {
            memcpy(L->pal, f.palette, sizeof(L->pal));
        }
    }
    s->core->FrameUnlock(s->core_ctx);
    if (L == NULL || L->bits == NULL) {
        if (L != NULL) {
            free(L);
        }
        log_msg(LOG_ERROR, "session %s: login: no memory for the login box",
                s->peer);
        return 0;
    }
    if (!rdp_same_mode(s, &f)) {
        rdp_take_mode(s, &f);
    }
    L->f.bits = L->bits;
    L->f.palette = f.bpp == 8 ? &L->pal[0][0] : NULL;
    for (i = 0; i < COLORS; i++) {
        L->col[i] = pixel(L, rgb[i]);
    }
    L->s = s;
    L->real = s->core;
    L->real_ctx = s->core_ctx;
    L->h = row_last(L) + LFONT_H + 16 + EDGE;
    L->x0 = ((int) f.width - BOX_W) / 2;
    L->y0 = ((int) f.height - L->h) / 2;
    if (L->x0 < 0) {
        L->x0 = 0;
    }
    if (L->y0 < 0) {
        L->y0 = 0;
    }
    if (!GetComputerNameA(name, &n)) {
        name[0] = '\0';
    }
    name[sizeof(name) - 1] = '\0';
    sprintf(L->title, "ntrdps - Log on%s%s", name[0] ? " to " : "", name);
    for (i = 0; s->ci_domain[i] != 0; i++) {
        L->text[F_USER][L->len[F_USER]++] = s->ci_domain[i];
    }
    if (L->len[F_USER] > 0) {
        L->text[F_USER][L->len[F_USER]++] = '\\';
    }
    for (i = 0; s->ci_user[i] != 0; i++) {
        L->text[F_USER][L->len[F_USER]++] = s->ci_user[i];
    }
    if (s->ci_user[0] == 0) {
        L->len[F_USER] = 0;
    }
    L->text[F_USER][L->len[F_USER]] = 0;
    L->focus = L->len[F_USER] > 0 ? F_PW : F_USER;
    strcpy(L->msg, msg);
    L->msg_color = C_RED;
    L->start_ms = GetTickCount();
    s->login = L;
    s->core = &login_api;
    s->core_ctx = L;
    L->dlg = ldlg_open(f.width, f.height, s->ci_user, s->ci_domain, wake, s,
                       s->peer);
    if (L->dlg != NULL) {
        unsigned char bg[3];

        ldlg_background(bg);
        fill_px(L, 0, 0, (int) f.width, (int) f.height, pixel(L, bg));
        damage(L, 0, 0, (int) f.width, (int) f.height);
        ldlg_picture(L->dlg, put, L);
    } else {
        log_msg(LOG_WARN, "session %s: login: ntrdps's own login box shows "
                "instead of the logon dialog", s->peer);
        draw_all(L);
    }
    return 1;
}

static void fail_msg(char *out, int left)
{
    sprintf(out, "Wrong user name or password. %d %s left.", left,
            left == 1 ? "try" : "tries");
}

int login_begin(SESSION *s, unsigned long *errinfo, const char **why)
{
    char msg[64];
    int r;

    *errinfo = ERRINFO_DENIED;
    msg[0] = '\0';
    if (!rdp_login_on()) {
        memset(s->ci_pw, 0, sizeof(s->ci_pw));
        return LOGIN_GO;
    }
    if (s->tls == NULL && !rdp_login_plain()) {
        memset(s->ci_pw, 0, sizeof(s->ci_pw));
        log_msg(LOG_WARN, "session %s: login: refused, this client uses "
                "plain RDP (setting LoginPlain 0: only TLS clients may log "
                "in)", s->peer);
        *why = "login: plain RDP is not allowed (setting LoginPlain 0)";
        return LOGIN_REFUSE;
    }
    if (!auth_usable()) {
        memset(s->ci_pw, 0, sizeof(s->ci_pw));
        log_msg(LOG_WARN, "session %s: login: refused, no ntrdps account "
                "is set (ntrdps -setlogin)", s->peer);
        *why = "login: no ntrdps account is set";
        return LOGIN_REFUSE;
    }
    if ((s->info_flags & INFO_AUTOLOGON) && s->ci_user[0] != 0) {
        r = auth_check(s->ci_user, s->ci_domain, s->ci_pw, s->peer);
        memset(s->ci_pw, 0, sizeof(s->ci_pw));
        s->phase_ms = GetTickCount();
        if (r == AUTH_OK) {
            return LOGIN_GO;
        }
        if (r != AUTH_BAD && r != AUTH_NOT_ADMIN) {
            *why = "login: the check failed (see the lines above)";
            return LOGIN_REFUSE;
        }
        strcpy(msg, "Wrong user name or password.");
    }
    memset(s->ci_pw, 0, sizeof(s->ci_pw));
    if (!make_box(s, msg)) {
        *errinfo = ERRINFO_OUT_OF_MEMORY;
        *why = "login: the login box could not start";
        return LOGIN_REFUSE;
    }
    log_msg(LOG_INFO, "session %s: login: the login screen shows (%s)",
            s->peer, s->tls != NULL ? "TLS" : "plain RDP: the password "
            "crosses the network in clear");
    return LOGIN_BOX;
}

static void check(LOGIN *L)
{
    static const WCHAR none[] = { 0 };
    SESSION *s = L->s;
    unsigned int locks = L->locks;
    char msg[64];
    int r;

    r = auth_check(L->text[F_USER], L->dlg != NULL ? L->domain : none,
                   L->text[F_PW], s->peer);
    memset(L->text[F_PW], 0, sizeof(L->text[F_PW]));
    L->len[F_PW] = 0;
    if (r == AUTH_OK) {
        log_msg(LOG_INFO, "session %s: login: done, the console comes",
                s->peer);
        login_free(s);
        rdp_console(s, locks);
        return;
    }
    if (r != AUTH_BAD && r != AUTH_NOT_ADMIN) {
        rdp_end(s, ERRINFO_DENIED, "login: the check failed (see the lines "
                "above)");
        return;
    }
    if (++L->tries >= LOGIN_TRIES) {
        log_msg(LOG_WARN, "session %s: login: %d wrong tries: the end",
                s->peer, L->tries);
        rdp_end(s, ERRINFO_DENIED, "login: too many wrong tries");
        return;
    }
    if (L->dlg != NULL) {
        ldlg_fail(L->dlg);
        return;
    }
    fail_msg(msg, LOGIN_TRIES - L->tries);
    L->focus = F_PW;
    set_msg(L, msg, C_RED);
    draw_field(L, F_USER);
    draw_field(L, F_PW);
}

static int dlg_pump(LOGIN *L)
{
    int ev;

    ldlg_picture(L->dlg, put, L);
    if (L->check_due) {
        return 1;
    }
    ev = ldlg_event(L->dlg, L->text[F_USER], L->domain, L->text[F_PW]);
    switch (ev) {
    case LDLG_SUBMIT:
        L->check_due = 1;
        L->check_ms = GetTickCount();
        break;
    case LDLG_CANCEL:
        log_msg(LOG_INFO, "session %s: login: Cancel in the logon dialog: "
                "the end", L->s->peer);
        rdp_end(L->s, 0, "login: cancelled");
        return 0;
    case LDLG_FAILED:
        rdp_end(L->s, ERRINFO_DENIED, "login: the logon dialog failed");
        return 0;
    }
    return 1;
}

void login_pump(SESSION *s)
{
    LOGIN *L = s->login;

    if (L == NULL || s->ending != END_NO) {
        return;
    }
    if (L->dlg != NULL && !dlg_pump(L)) {
        return;
    }
    if (!L->check_due) {
        return;
    }
    if (GetTickCount() - L->check_ms < 1000 && !s->suppressed &&
        (SESSION_QUEUED(s) > 0 || s->map.dirty > 0 || s->tile.ready)) {
        return;
    }
    L->check_due = 0;
    check(L);
}

void login_check_time(SESSION *s, DWORD now)
{
    LOGIN *L = s->login;

    if (L == NULL || L->check_due || s->ending != END_NO ||
        now - L->start_ms <= LOGIN_TIMEOUT_MS) {
        return;
    }
    log_msg(LOG_WARN, "session %s: login: no log on within %d s: the end",
            s->peer, LOGIN_TIMEOUT_MS / 1000);
    rdp_end(s, ERRINFO_LOGON_TIMEOUT, "login: no log on in time");
}
