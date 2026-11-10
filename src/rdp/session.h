/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_SESSION_H
#define NTRDPS_SESSION_H

#include <windows.h>
#include <winsock.h>
#include "net.h"
#include "core.h"
#include "orders.h"

#define ST_WAIT_CR      0
#define ST_WAIT_CI      1
#define ST_WAIT_DOMAIN  2
#define ST_WAIT_JOINS   3
#define ST_WAIT_CONFIRM 4
#define ST_FINALIZE     5
#define ST_ACTIVE       6

#define SESS_EV_FRAME   1
#define SESS_EV_STOP    2
#define SESS_EV_POINTER 4
#define SESS_EV_POS     8
#define SESS_EV_HOLD    16
#define SESS_EV_SHOW    32
#define SESS_EV_RECTS   64
#define SESS_EV_ORDERS  128

#define END_NO    0
#define END_FLUSH 1
#define END_NOW   2

#define SESS_RX_MAX 65536
#define SESS_TX_MAX 65536

typedef struct {
    unsigned long  have;
    unsigned int   extra_flags;
    unsigned int   bitmap_compression;
    unsigned int   ask_bpp, ask_width, ask_height;
    unsigned int   order_flags;
    unsigned char  order_support[32];
    unsigned int   text_flags;
    unsigned long  desktop_save_size;
    unsigned int   bmc_entries[3];
    unsigned int   bmc_cell_size[3];
    unsigned int   color_pointer;
    unsigned int   color_pointer_cache;
    unsigned int   pointer_cache;
    unsigned int   pointer_new;
    unsigned int   color_table_cache;
    unsigned int   input_flags;
    unsigned long  input_kbd_layout;
    unsigned long  brush_level;
    unsigned char  glyph[48];
    unsigned char  offscreen[8];
    unsigned char  bmc2[36];
    int            freerdp;
} CLIENT_CAPS;

#define UPDATE_TILE_BYTES  8128
#define UPDATE_TILE_HEADER 18

#define UPDATE_CELL_W    64
#define UPDATE_CELLS_MAX 6400

typedef struct {
    unsigned char x0, y0, x1, y1;
} UPDATE_CELL;

typedef struct {
    unsigned int cols, rows;
    unsigned int cell_h;
    unsigned int dirty;
    unsigned int next;
    UPDATE_CELL  cell[UPDATE_CELLS_MAX];
    unsigned char sent[UPDATE_CELLS_MAX];
    unsigned int unsent;
    unsigned long at[UPDATE_CELLS_MAX];
} UPDATE_MAP;

typedef struct {
    int           ready;
    unsigned int  x, y, w, h;
    unsigned int  flags;
    unsigned int  len;
    unsigned char data[UPDATE_TILE_BYTES - UPDATE_TILE_HEADER];
} UPDATE_TILE;

#define POINTER_SLOTS 20

typedef struct {
    unsigned int  hot_x, hot_y;
    unsigned char and_mask[CORE_POINTER_MAX][CORE_POINTER_MAX / 8];
    unsigned char color[CORE_POINTER_MAX][CORE_POINTER_MAX * 3];
} POINTER_SHAPE;

#define PTR_SHOWN_NONE   0
#define PTR_SHOWN_ARROW  1
#define PTR_SHOWN_HIDDEN 2
#define PTR_SHOWN_SLOT   3

typedef struct {
    unsigned int  slots;
    unsigned int  filled;
    unsigned long used[POINTER_SLOTS];
    unsigned long clock;
    POINTER_SHAPE slot[POINTER_SLOTS];
    int           shown;
    unsigned int  shown_slot;
    int           shape_due;
    int           pos_due;
    int           pos_x, pos_y;
    unsigned long sent_new, sent_cached, sent_system, sent_pos;
} POINTER_STATE;

#define ORDER_PDU_BYTES 8124

typedef struct {
    unsigned int  count;
    unsigned int  len;
    unsigned char data[ORDER_PDU_BYTES];
} ORDER_PDU;

#define GLYPH_CACHES 10
#define GLYPH_SLOTS  254

#define GLYPHS_W_SETUP  0
#define GLYPHS_W_RUN    1
#define GLYPHS_W_GLYPH  2
#define GLYPHS_W_RANGE  3
#define GLYPHS_W_CACHE  4
#define GLYPHS_W_CODE   5
#define GLYPHS_W_ROOM   6
#define GLYPHS_WHYS     7

typedef struct {
    unsigned int   entries;
    unsigned int   cell;
    unsigned short place[GLYPH_SLOTS];
    unsigned long  serial[GLYPH_SLOTS];
    unsigned long  used[GLYPH_SLOTS];
    unsigned char  slot_of[CORE_GLYPH_PLACES];
} GLYPH_CACHE;

typedef struct {
    int           level;
    int           usable;
    GLYPH_CACHE   cache[GLYPH_CACHES];
    unsigned long fills;
    unsigned long glyphs;
    unsigned long fill_bytes;
    unsigned long why[GLYPHS_WHYS];
} GLYPH_STATE;

#define SAVE_LEVELS 8

typedef struct {
    unsigned long num;
    RECT          r;
    unsigned long pos;
    unsigned long pels;
    RECT          stale;
} SAVE_ENTRY;

typedef struct {
    unsigned long size;
    int           n;
    SAVE_ENTRY    e[SAVE_LEVELS];
    unsigned long saves;
    unsigned long restores;
    unsigned long bitmaps;
    unsigned long refused;
} SAVE_STATE;

#define BMP_CACHES 3
#define BMP_SLOTS  600

typedef struct {
    unsigned int  entries;
    unsigned long cell;
    unsigned long key[BMP_SLOTS][2];
    unsigned char cx[BMP_SLOTS];
    unsigned char cy[BMP_SLOTS];
    unsigned long used[BMP_SLOTS];
} BMP_CACHE;

typedef struct {
    int           rev;
    int           usable;
    int           table_sent;
    unsigned long clock;
    BMP_CACHE     cache[BMP_CACHES];
    unsigned long hits;
    unsigned long fills;
    unsigned long fill_rle;
    unsigned long fill_bytes;
    unsigned long tables;
    unsigned long gone;
    unsigned long nofit;
    unsigned long notake;
} BMP_STATE;

typedef struct {
    unsigned long tiles_rle, tiles_raw;
    unsigned long pixel_bytes;
    unsigned long data_bytes;
    double        rle_ms;
    int           first_logged;
    unsigned long ord_sent;
    unsigned long ord_type[ORD_TYPES];
    unsigned long ord_bytes;
    unsigned long ord_pdus;
    unsigned long ord_bitmap;
    unsigned long ord_covered;
    unsigned long ord_fringe;
    unsigned long ord_lost;
} UPDATE_STATS;

typedef struct {
    SOCKET         sock;
    char           peer[NET_PEER_MAX];
    volatile LONG *stop;
    int            state;
    int            ending;
    char           why[160];
    DWORD          start_ms;
    DWORD          end_ms;
    DWORD          phase_ms;

    unsigned char  rx[SESS_RX_MAX];
    unsigned int   rx_len;
    unsigned char  tx[SESS_TX_MAX];
    unsigned int   tx_pos, tx_len;

    int                want_tls;
    struct tls_conn   *tls;
    unsigned long      client_protocols;

    unsigned long  rx_bytes, rx_packets;
    unsigned long  tx_bytes, tx_packets;
    unsigned long  fastpath;
    unsigned long  in_events;

    unsigned int   core_len;
    unsigned long  client_version;
    unsigned int   client_width, client_height;
    unsigned int   high_depth;
    unsigned int   supported_depths;
    unsigned int   early_flags;
    unsigned long  kbd_layout, kbd_type, kbd_subtype, kbd_fkeys;
    unsigned int   channels;

    unsigned long  info_flags;
    char           user[64];
    struct MPPC_ENC_s *mppc;

    const CORE_API *core;
    void           *core_ctx;
    void           *core_session;
    unsigned int    buttons;
    int             mouse_x, mouse_y;
    int             mouse_seen;
    DWORD           mouse_ms;

    CRITICAL_SECTION lock;
    int             ev_flags;
    RECT            ev_rects[SESS_EV_RECTS];
    unsigned long   ev_at[SESS_EV_RECTS];
    int             ev_nrects;
    int             ev_x, ev_y;
    int             ev_stop;

    SOCKET             wake;
    struct sockaddr_in wake_to;
    volatile LONG      wake_due;
    volatile int       wake_off;
    unsigned int       wake_bad;
    unsigned long      turns;
    unsigned long      turns_none;
    unsigned long      wake_reads;
    unsigned long      wake_errors;
    DWORD              turns_ms;

    int            console_depth;
    int            depth;
    unsigned int   user_id;
    int            licensed;
    unsigned int   width, height;
    unsigned long  share_id;
    unsigned int   generation;
    CLIENT_CAPS    caps;
    int            mode_due;

    int            held;
    int            suppressed;
    int            upd_palette;
    unsigned char *reduce;
    UPDATE_MAP     map;
    UPDATE_TILE    tile;
    UPDATE_STATS   stats;
    ORDER_ENC      ord;
    unsigned int   ord_allow;
    unsigned long  ord_next;
    unsigned long  map_now;
    int            ord_due;
    int            ord_skip;
    ORDER_PDU      ord_pdu;
    GLYPH_STATE    glyph;
    SAVE_STATE     save;
    BMP_STATE      bmp;
    POINTER_STATE  ptr;

    unsigned char  t2_logged[32];
    unsigned int   ev_logged[16];
    unsigned int   ev_logged_n;
} SESSION;

#define SESSION_QUEUED(s) ((s)->tx_len - (s)->tx_pos)

void session_run(SOCKET sock, const char *peer, volatile LONG *stop);

int session_send(SESSION *s, const unsigned char *pkt, unsigned int len);

void session_end(SESSION *s, int flush, const char *fmt, ...);

void session_wake(SESSION *s);

#ifdef NTRDPS_TLS
int session_input_waiting(SESSION *s);
#endif

#endif
