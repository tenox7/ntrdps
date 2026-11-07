/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <string.h>
#include "log.h"
#include "keymap.h"

#define VK_TAB_      0x09
#define VK_PAUSE_    0x13
#define VK_CAPITAL_  0x14
#define VK_NUMLOCK_  0x90
#define VK_SCROLL_   0x91
#define VK_LCONTROL_ 0xA2
#define VK_RMENU_    0xA5
#define VK_LWIN_     0x5B
#define VK_RWIN_     0x5C
#define VK_APPS_     0x5D

#define SC_LCTRL     0x1D
#define SC_LSHIFT    0x2A
#define SC_RSHIFT    0x36
#define SC_ALT       0x38
#define SC_NUMLOCK   0x45
#define SC_DEL       0x53
#define SC_TAB       0x0F

typedef struct {
    unsigned char code, vk;
} PAIR;

static const PAIR e0_keys[] = {
    { 0x1C, 0x0D },
    { 0x1D, 0xA3 },
    { 0x35, 0x6F },
    { 0x37, 0x2C },
    { 0x38, 0xA5 },
    { 0x46, 0x03 },
    { 0x47, 0x24 },
    { 0x48, 0x26 },
    { 0x49, 0x21 },
    { 0x4B, 0x25 },
    { 0x4D, 0x27 },
    { 0x4F, 0x23 },
    { 0x50, 0x28 },
    { 0x51, 0x22 },
    { 0x52, 0x2D },
    { 0x53, 0x2E },
    { 0x5B, VK_LWIN_ },
    { 0x5C, VK_RWIN_ },
    { 0x5D, VK_APPS_ }
};

static const PAIR plain_keys[] = {
    { SC_LCTRL, VK_LCONTROL_ },
    { SC_LSHIFT, 0xA0 },
    { SC_RSHIFT, 0xA1 },
    { SC_ALT, 0xA4 },
    { SC_NUMLOCK, VK_NUMLOCK_ }
};

static const struct {
    unsigned char code, on, off;
} numpad[] = {
    { 0x47, 0x67, 0x24 },
    { 0x48, 0x68, 0x26 },
    { 0x49, 0x69, 0x21 },
    { 0x4B, 0x64, 0x25 },
    { 0x4C, 0x65, 0x0C },
    { 0x4D, 0x66, 0x27 },
    { 0x4F, 0x61, 0x23 },
    { 0x50, 0x62, 0x28 },
    { 0x51, 0x63, 0x22 },
    { 0x52, 0x60, 0x2D },
    { 0x53, 0x6E, 0x2E }
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static int held_any(const KEYMAP *k, unsigned int code)
{
    return k->held[0][code] != 0 || k->held[1][code] != 0;
}

static int ctrl_down(const KEYMAP *k)
{
    return held_any(k, SC_LCTRL) || k->e1_ctrl || k->altgr_ctrl;
}

static void inject(KEYMAP *k, unsigned int vk, unsigned int code, int e0,
                   int down)
{
    unsigned int flags = down ? 0 : KM_UP;

    if (e0 || vk == VK_NUMLOCK_) {
        flags |= KM_EXTENDED;
    }
    k->emit(k->ctx, vk, code, flags);
}

static void log_once(KEYMAP *k, unsigned int code, int e0, const char *why)
{
    unsigned char bit = (unsigned char) (1 << (code % 8));

    if ((k->logged[e0][code / 8] & bit) == 0) {
        k->logged[e0][code / 8] |= bit;
        log_msg(LOG_INFO, "input: key %s0x%02X: %s", e0 ? "E0 " : "",
                code, why);
    }
}

static unsigned int find_vk(KEYMAP *k, unsigned int code, int e0)
{
    unsigned int i, vk = 0;

    if (e0) {
        if (code == SC_LSHIFT || code == SC_RSHIFT) {
            return 0;
        }
        for (i = 0; i < COUNT(e0_keys); i++) {
            if (e0_keys[i].code == code) {
                vk = e0_keys[i].vk;
            }
        }
        if (!k->win_keys &&
            (vk == VK_LWIN_ || vk == VK_RWIN_ || vk == VK_APPS_)) {
            log_once(k, code, e0, "no such key on NT 3.1: dropped "
                     "(rdp-2 4.2.1)");
            return 0;
        }
    } else {
        for (i = 0; i < COUNT(plain_keys); i++) {
            if (plain_keys[i].code == code) {
                vk = plain_keys[i].vk;
            }
        }
        for (i = 0; vk == 0 && i < COUNT(numpad); i++) {
            if (numpad[i].code == code) {
                vk = k->numlock && !held_any(k, SC_LSHIFT) &&
                     !held_any(k, SC_RSHIFT) ? numpad[i].on : numpad[i].off;
            }
        }
        if (vk == 0) {
            vk = k->map_vk(k->ctx, code) & 0xFF;
        }
    }
    if (vk == 0) {
        log_once(k, code, e0, "no VK: dropped (rdp-2 4.2 step 6)");
    }
    return vk;
}

static int e1_step(KEYMAP *k, unsigned int code, unsigned int prefix,
                   int down)
{
    int fake_ctrl = prefix == 0xE1 && code == SC_LCTRL;

    switch (k->e1) {
    case KM_E1_HELD:
        if (prefix == 0 && code == SC_NUMLOCK && down) {
            k->e1 = KM_E1_PAUSE;
            k->pause_down = 1;
            k->emit(k->ctx, VK_PAUSE_, SC_NUMLOCK, 0);
            return 1;
        }
        inject(k, VK_LCONTROL_, SC_LCTRL, 0, 1);
        k->e1_ctrl = 1;
        k->e1 = KM_E1_NONE;
        break;
    case KM_E1_PAUSE:
        if (fake_ctrl && !down) {
            return 1;
        }
        k->e1 = KM_E1_NONE;
        if (prefix == 0 && code == SC_NUMLOCK && !down) {
            k->pause_down = 0;
            k->emit(k->ctx, VK_PAUSE_, SC_NUMLOCK, KM_UP);
            return 1;
        }
        break;
    }
    if (prefix != 0xE1) {
        return 0;
    }
    if (fake_ctrl && down) {
        k->e1 = KM_E1_HELD;
    } else if (fake_ctrl) {
        inject(k, VK_LCONTROL_, SC_LCTRL, 0, 0);
        k->e1_ctrl = 0;
    } else {
        log_once(k, code, 0, "with the E1 prefix: dropped (rdp-2 4.3.1)");
    }
    return 1;
}

void keymap_init(KEYMAP *k)
{
    memset(k->held, 0, sizeof(k->held));
    memset(k->eaten, 0, sizeof(k->eaten));
    memset(k->logged, 0, sizeof(k->logged));
    k->numlock = 0;
    k->e1 = KM_E1_NONE;
    k->e1_ctrl = 0;
    k->pause_down = 0;
    k->altgr_ctrl = 0;
}

void keymap_key(KEYMAP *k, unsigned int code, unsigned int prefix, int down)
{
    int e0 = prefix == 0xE0;
    unsigned int vk;

    code &= 0x7F;
    if ((prefix == 0xE1 || k->e1 != KM_E1_NONE) &&
        e1_step(k, code, prefix, down)) {
        return;
    }
    if (code == SC_DEL) {
        if (k->eaten[e0]) {
            if (!down) {
                k->eaten[e0] = 0;
            }
            return;
        }
        if (down && k->held[e0][code] == 0 && held_any(k, SC_LCTRL) &&
            held_any(k, SC_ALT)) {
            k->eaten[e0] = 1;
            k->sas(k->ctx);
            return;
        }
    }
    if (down) {
        vk = k->held[e0][code];
        if (vk == 0) {
            vk = find_vk(k, code, e0);
            if (vk == 0) {
                return;
            }
            if (vk == VK_RMENU_ && k->altgr_fake_ctrl && !ctrl_down(k)) {
                inject(k, VK_LCONTROL_, SC_LCTRL, 0, 1);
                k->altgr_ctrl = 1;
            }
            if (vk == VK_NUMLOCK_) {
                k->numlock = !k->numlock;
            }
            k->held[e0][code] = (unsigned char) vk;
        }
        inject(k, vk, code, e0, 1);
        return;
    }
    vk = k->held[e0][code];
    if (vk != 0) {
        k->held[e0][code] = 0;
    } else {
        vk = find_vk(k, code, e0);
        if (vk == 0) {
            return;
        }
    }
    inject(k, vk, code, e0, 0);
    if (vk == VK_RMENU_ && k->altgr_ctrl) {
        inject(k, VK_LCONTROL_, SC_LCTRL, 0, 0);
        k->altgr_ctrl = 0;
    }
}

void keymap_release_all(KEYMAP *k)
{
    unsigned int code, n = 0;
    int e0;

    if (held_any(k, SC_ALT)) {
        k->emit(k->ctx, VK_TAB_, SC_TAB, KM_UP);
    }
    for (e0 = 0; e0 < 2; e0++) {
        for (code = 0; code < 128; code++) {
            if (k->held[e0][code] != 0) {
                inject(k, k->held[e0][code], code, e0, 0);
                k->held[e0][code] = 0;
                n++;
            }
        }
    }
    if (k->pause_down) {
        k->emit(k->ctx, VK_PAUSE_, SC_NUMLOCK, KM_UP);
        n++;
    }
    if (k->e1_ctrl || k->altgr_ctrl) {
        inject(k, VK_LCONTROL_, SC_LCTRL, 0, 0);
        n++;
    }
    k->e1 = KM_E1_NONE;
    k->e1_ctrl = 0;
    k->pause_down = 0;
    k->altgr_ctrl = 0;
    k->eaten[0] = 0;
    k->eaten[1] = 0;
    if (n > 0) {
        log_msg(LOG_INFO, "input: %u held key%s released", n,
                n == 1 ? "" : "s");
    }
}

void keymap_sync(KEYMAP *k, unsigned int locks, int console_known,
                 unsigned int console)
{
    static const struct {
        unsigned int bit, vk, code;
    } lock[3] = {
        { KM_LOCK_CAPS, VK_CAPITAL_, 0x3A },
        { KM_LOCK_NUM, VK_NUMLOCK_, SC_NUMLOCK },
        { KM_LOCK_SCROLL, VK_SCROLL_, 0x46 }
    };
    int i;

    keymap_release_all(k);
    for (i = 0; console_known && i < 3; i++) {
        if ((locks ^ console) & lock[i].bit) {
            inject(k, lock[i].vk, lock[i].code, 0, 1);
            inject(k, lock[i].vk, lock[i].code, 0, 0);
        }
    }
    k->numlock = (locks & KM_LOCK_NUM) != 0;
}
