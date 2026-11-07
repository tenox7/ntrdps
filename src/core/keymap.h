/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_KEYMAP_H
#define NTRDPS_KEYMAP_H

#define KM_EXTENDED 0x0001
#define KM_UP       0x0002

#define KM_LOCK_SCROLL 1
#define KM_LOCK_NUM    2
#define KM_LOCK_CAPS   4

#define KM_E1_NONE  0
#define KM_E1_HELD  1
#define KM_E1_PAUSE 2

typedef struct {
    void *ctx;
    void (*emit)(void *ctx, unsigned int vk, unsigned int scan,
                 unsigned int flags);
    unsigned int (*map_vk)(void *ctx, unsigned int code);
    void (*sas)(void *ctx);
    int win_keys;

    int altgr_fake_ctrl;

    unsigned char held[2][128];
    unsigned char eaten[2];
    unsigned char logged[2][16];
    int numlock;
    int e1;
    int e1_ctrl;
    int pause_down;
    int altgr_ctrl;
} KEYMAP;

void keymap_init(KEYMAP *k);

void keymap_key(KEYMAP *k, unsigned int code, unsigned int prefix, int down);

void keymap_release_all(KEYMAP *k);

void keymap_sync(KEYMAP *k, unsigned int locks, int console_known,
                 unsigned int console);

#endif
