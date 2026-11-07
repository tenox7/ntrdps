/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <stdio.h>
#include "keymap.h"
#include "tcheck.h"

#define MAXEV 64

static unsigned int g_ev[MAXEV][3];
static int g_n;
static int g_sas;
static KEYMAP g_k;

static void emit(void *ctx, unsigned int vk, unsigned int scan,
                 unsigned int flags)
{
    if (g_n < MAXEV) {
        g_ev[g_n][0] = vk;
        g_ev[g_n][1] = scan;
        g_ev[g_n][2] = flags;
    }
    g_n++;
}

static unsigned int map_vk(void *ctx, unsigned int code)
{
    switch (code) {
    case 0x0F: return 0x09;
    case 0x10: return 'Q';
    case 0x1E: return 'A';
    case 0x23: return 'H';
    case 0x2C: return 'Z';
    case 0x37: return 0x6A;
    }
    return 0;
}

static void sas(void *ctx)
{
    g_sas++;
}

static void expect(const unsigned int *w, int n, int line)
{
    int i;

    if (g_n != n) {
        t_failures++;
        printf("t_keymap.c(%d): %d events, want %d:", line, g_n, n);
        for (i = 0; i < g_n && i < MAXEV; i++) {
            printf(" (%02X %02X %X)", g_ev[i][0], g_ev[i][1], g_ev[i][2]);
        }
        printf("\n");
    } else {
        for (i = 0; i < n; i++) {
            if (g_ev[i][0] != w[i * 3] || g_ev[i][1] != w[i * 3 + 1] ||
                g_ev[i][2] != w[i * 3 + 2]) {
                t_failures++;
                printf("t_keymap.c(%d): event %d is (%02X %02X %X), want "
                       "(%02X %02X %X)\n", line, i, g_ev[i][0], g_ev[i][1],
                       g_ev[i][2], w[i * 3], w[i * 3 + 1], w[i * 3 + 2]);
            }
        }
    }
    g_n = 0;
}

#define EXPECT(a) expect((a), (int) (sizeof(a) / sizeof((a)[0]) / 3), \
                         __LINE__)
#define EXPECT_NONE() expect(NULL, 0, __LINE__)

#define X  KM_EXTENDED
#define U  KM_UP

static void reset(int altgr, int win_keys)
{
    g_k.ctx = NULL;
    g_k.emit = emit;
    g_k.map_vk = map_vk;
    g_k.sas = sas;
    g_k.win_keys = win_keys;
    g_k.altgr_fake_ctrl = altgr;
    keymap_init(&g_k);
    g_n = 0;
    g_sas = 0;
}

static void key(unsigned int code, unsigned int prefix, int down)
{
    keymap_key(&g_k, code, prefix, down);
}

static void test_plain(void)
{
    static const unsigned int h[] = { 'H', 0x23, 0, 'H', 0x23, 0, 'H', 0x23, 0,
                                      'H', 0x23, U };
    static const unsigned int tab_up[] = { 0x09, 0x0F, U };
    static const unsigned int mods[] = { 0xA2, 0x1D, 0, 0xA0, 0x2A, 0,
                                         0xA1, 0x36, 0, 0xA4, 0x38, 0,
                                         0xA4, 0x38, U, 0xA1, 0x36, U,
                                         0xA0, 0x2A, U, 0xA2, 0x1D, U };
    static const unsigned int star[] = { 0x6A, 0x37, 0 };

    reset(0, 1);
    key(0x23, 0, 1);
    key(0x23, 0, 1);
    key(0x23, 0, 1);
    key(0x23, 0, 0);
    EXPECT(h);
    CHECK_EQ(g_k.held[0][0x23], 0);
    key(0x0F, 0, 0);
    EXPECT(tab_up);
    key(0x1D, 0, 1);
    key(0x2A, 0, 1);
    key(0x36, 0, 1);
    key(0x38, 0, 1);
    key(0x38, 0, 0);
    key(0x36, 0, 0);
    key(0x2A, 0, 0);
    key(0x1D, 0, 0);
    EXPECT(mods);
    key(0x37, 0, 1);
    EXPECT(star);
    key(0x37, 0, 0);
    g_n = 0;
    key(0x59, 0, 1);
    key(0x59, 0, 0);
    EXPECT_NONE();
}

static void test_e0(void)
{
    static const unsigned int keys[] = { 0xA3, 0x1D, X, 0x26, 0x48, X,
                                         0x26, 0x48, X | U, 0xA3, 0x1D, X | U,
                                         0x2E, 0x53, X, 0x2E, 0x53, X | U };
    static const unsigned int win[] = { 0x5B, 0x5B, X, 0x5B, 0x5B, X | U };

    reset(0, 1);
    key(0x1D, 0xE0, 1);
    key(0x48, 0xE0, 1);
    key(0x48, 0xE0, 0);
    key(0x1D, 0xE0, 0);
    key(0x53, 0xE0, 1);
    key(0x53, 0xE0, 0);
    EXPECT(keys);
    key(0x2A, 0xE0, 1);
    key(0x2A, 0xE0, 0);
    key(0x10, 0xE0, 1);
    EXPECT_NONE();
    key(0x5B, 0xE0, 1);
    key(0x5B, 0xE0, 0);
    EXPECT(win);
    reset(0, 0);
    key(0x5B, 0xE0, 1);
    key(0x5B, 0xE0, 0);
    EXPECT_NONE();
}

static void test_numpad(void)
{
    static const unsigned int off[] = { 0x24, 0x47, 0, 0x24, 0x47, U };
    static const unsigned int on[] = { 0x67, 0x47, 0, 0x67, 0x47, U,
                                       0x6E, 0x53, 0, 0x6E, 0x53, U };
    static const unsigned int shift[] = { 0xA0, 0x2A, 0, 0x24, 0x47, 0,
                                          0x24, 0x47, U, 0xA0, 0x2A, U };
    static const unsigned int stored[] = { 0x67, 0x47, 0, 0x90, 0x45, X,
                                           0x90, 0x45, X | U, 0x67, 0x47, U };
    static const unsigned int nl_repeat[] = { 0x90, 0x45, X, 0x90, 0x45, X,
                                              0x90, 0x45, X | U };

    reset(0, 1);
    key(0x47, 0, 1);
    key(0x47, 0, 0);
    EXPECT(off);
    keymap_sync(&g_k, KM_LOCK_NUM, 0, 0);
    EXPECT_NONE();
    CHECK_EQ(g_k.numlock, 1);
    key(0x47, 0, 1);
    key(0x47, 0, 0);
    key(0x53, 0, 1);
    key(0x53, 0, 0);
    EXPECT(on);
    key(0x2A, 0, 1);
    key(0x47, 0, 1);
    key(0x47, 0, 0);
    key(0x2A, 0, 0);
    EXPECT(shift);
    key(0x47, 0, 1);
    key(0x45, 0, 1);
    key(0x45, 0, 0);
    key(0x47, 0, 0);
    EXPECT(stored);
    CHECK_EQ(g_k.numlock, 0);
    key(0x45, 0, 1);
    key(0x45, 0, 1);
    key(0x45, 0, 0);
    EXPECT(nl_repeat);
    CHECK_EQ(g_k.numlock, 1);
}

static void test_pause(void)
{
    static const unsigned int pause[] = { 0x13, 0x45, 0, 0x13, 0x45, U };
    static const unsigned int ctrl[] = { 0xA2, 0x1D, 0, 'A', 0x1E, 0,
                                         'A', 0x1E, U, 0xA2, 0x1D, U };
    static const unsigned int stuck[] = { 0x13, 0x45, 0, 'A', 0x1E, 0,
                                          'A', 0x1E, U, 0x13, 0x45, U };

    reset(0, 1);
    key(0x1D, 0xE1, 1);
    key(0x45, 0, 1);
    key(0x1D, 0xE1, 0);
    key(0x45, 0, 0);
    EXPECT(pause);
    CHECK_EQ(g_k.e1, KM_E1_NONE);
    key(0x1D, 0xE1, 1);
    key(0x1E, 0, 1);
    key(0x1E, 0, 0);
    key(0x1D, 0xE1, 0);
    EXPECT(ctrl);
    CHECK_EQ(g_k.e1_ctrl, 0);
    key(0x1D, 0xE1, 1);
    key(0x45, 0, 1);
    key(0x1E, 0, 1);
    key(0x1E, 0, 0);
    keymap_release_all(&g_k);
    EXPECT(stuck);
    key(0x1D, 0xE1, 1);
    keymap_release_all(&g_k);
    EXPECT_NONE();
    CHECK_EQ(g_k.e1, KM_E1_NONE);
}

static void test_altgr(void)
{
    static const unsigned int fake[] = { 0xA2, 0x1D, 0, 0xA5, 0x38, X,
                                         0xA5, 0x38, X, 'Q', 0x10, 0,
                                         'Q', 0x10, U, 0xA5, 0x38, X | U,
                                         0xA2, 0x1D, U };
    static const unsigned int e1fake[] = { 0xA2, 0x1D, 0, 0xA5, 0x38, X,
                                           'Q', 0x10, 0, 'Q', 0x10, U,
                                           0xA5, 0x38, X | U, 0xA2, 0x1D, U };
    static const unsigned int ms51[] = { 0xA2, 0x1D, 0, 0xA5, 0x38, X,
                                         'Q', 0x10, 0, 'Q', 0x10, U,
                                         0xA2, 0x1D, U, 0xA5, 0x38, X | U };
    static const unsigned int plain[] = { 0xA5, 0x38, X, 0xA5, 0x38, X | U };

    reset(1, 1);
    key(0x38, 0xE0, 1);
    key(0x38, 0xE0, 1);
    key(0x10, 0, 1);
    key(0x10, 0, 0);
    key(0x38, 0xE0, 0);
    EXPECT(fake);
    CHECK_EQ(g_k.altgr_ctrl, 0);
    key(0x1D, 0xE1, 1);
    key(0x38, 0xE0, 1);
    key(0x10, 0, 1);
    key(0x10, 0, 0);
    key(0x38, 0xE0, 0);
    key(0x1D, 0xE1, 0);
    EXPECT(e1fake);
    key(0x1D, 0, 1);
    key(0x38, 0xE0, 1);
    key(0x10, 0, 1);
    key(0x10, 0, 0);
    key(0x1D, 0, 0);
    key(0x38, 0xE0, 0);
    EXPECT(ms51);
    CHECK_EQ(g_k.altgr_ctrl, 0);
    reset(0, 1);
    key(0x38, 0xE0, 1);
    key(0x38, 0xE0, 0);
    EXPECT(plain);
}

static void test_cad(void)
{
    static const unsigned int down[] = { 0xA2, 0x1D, 0, 0xA4, 0x38, 0 };
    static const unsigned int up[] = { 0xA4, 0x38, U, 0xA2, 0x1D, U };
    static const unsigned int alt_del[] = { 0xA4, 0x38, 0, 0x2E, 0x53, 0,
                                            0x2E, 0x53, U, 0xA4, 0x38, U };

    reset(0, 1);
    key(0x1D, 0, 1);
    key(0x38, 0, 1);
    EXPECT(down);
    key(0x53, 0, 1);
    key(0x53, 0, 1);
    key(0x53, 0, 0);
    EXPECT_NONE();
    CHECK_EQ(g_sas, 1);
    key(0x38, 0, 0);
    key(0x1D, 0, 0);
    EXPECT(up);
    key(0x1D, 0xE0, 1);
    key(0x38, 0xE0, 1);
    key(0x53, 0xE0, 1);
    key(0x53, 0xE0, 0);
    CHECK_EQ(g_sas, 2);
    key(0x38, 0xE0, 0);
    key(0x1D, 0xE0, 0);
    g_n = 0;
    key(0x38, 0, 1);
    key(0x53, 0, 1);
    key(0x53, 0, 0);
    key(0x38, 0, 0);
    EXPECT(alt_del);
    CHECK_EQ(g_sas, 2);
}

static void test_release_sync(void)
{
    static const unsigned int rel[] = { 0x09, 0x0F, U, 'A', 0x1E, U,
                                        0xA0, 0x2A, U, 0xA4, 0x38, U,
                                        0x26, 0x48, X | U };
    static const unsigned int sync[] = { 0x14, 0x3A, 0, 0x14, 0x3A, U,
                                         0x91, 0x46, 0, 0x91, 0x46, U };

    reset(0, 1);
    key(0x2A, 0, 1);
    key(0x1E, 0, 1);
    key(0x38, 0, 1);
    key(0x48, 0xE0, 1);
    g_n = 0;
    keymap_release_all(&g_k);
    EXPECT(rel);
    keymap_release_all(&g_k);
    EXPECT_NONE();
    keymap_sync(&g_k, KM_LOCK_CAPS | KM_LOCK_NUM, 1,
                KM_LOCK_NUM | KM_LOCK_SCROLL);
    EXPECT(sync);
    CHECK_EQ(g_k.numlock, 1);
    keymap_sync(&g_k, 0, 1, 0);
    EXPECT_NONE();
    CHECK_EQ(g_k.numlock, 0);
}

int main(void)
{
    test_plain();
    test_e0();
    test_numpad();
    test_pause();
    test_altgr();
    test_cad();
    test_release_sync();
    return t_result("t_keymap");
}
