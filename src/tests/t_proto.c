/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "frame.h"
#include "session.h"
#include "rdp.h"
#include "gcc.h"
#include "mcs.h"
#include "update.h"
#include "pointer.h"
#include "inputpdu.h"
#include "prompt.h"
#include "reduce.h"
#include "rledec.h"
#include "caps.h"
#include "share.h"
#include "mppc.h"
#include "palette.h"
#include "tcheck.h"
#include "tv_conn.h"

static SESSION g_s;
static SESSION g_s2;
static SESSION *g_t = &g_s;
static unsigned char g_pkt[4096];

static unsigned int hex(unsigned char *dst, unsigned int cap,
                        const char *text)
{
    unsigned int n = 0;
    int hi = -1, v;

    for (; *text; text++) {
        if (*text >= '0' && *text <= '9') {
            v = *text - '0';
        } else if (*text >= 'A' && *text <= 'F') {
            v = *text - 'A' + 10;
        } else if (*text >= 'a' && *text <= 'f') {
            v = *text - 'a' + 10;
        } else {
            continue;
        }
        if (hi < 0) {
            hi = v;
        } else if (n < cap) {
            dst[n++] = (unsigned char) (hi * 16 + v);
            hi = -1;
        }
    }
    return n;
}

static int g_open;
static int g_open2;

static void reset(void)
{
    int *open = g_t == &g_s ? &g_open : &g_open2;

    if (*open) {
        rdp_done(g_t);
    }
    memset(g_t, 0, sizeof(*g_t));
    strcpy(g_t->peer, g_t == &g_s ? "test" : "second");
    rdp_init(g_t);
    *open = 1;
}

static void print_hex(const char *what, const unsigned char *p,
                      unsigned int n)
{
    unsigned int i;

    printf("  %s (%u bytes):", what, n);
    for (i = 0; i < n; i++) {
        printf("%s%02X", i % 16 ? " " : "\n    ", p[i]);
    }
    printf("\n");
}

static void feed(const char *text)
{
    unsigned int n, len;
    int kind;

    n = hex(g_pkt, sizeof(g_pkt), text);
    kind = frame_length(g_pkt, n, &len);
    CHECK(kind == FRAME_TPKT || kind == FRAME_FASTPATH);
    CHECK_EQ(len, n);
    rdp_packet(g_t, kind, g_pkt, n);
}

static void expect_pkt(const char *text, int line)
{
    unsigned char want[4096];
    unsigned int n = hex(want, sizeof(want), text);

    if (memcmp(g_pkt, want, n) != 0) {
        t_failures++;
        printf("t_proto.c(%d): the packet buffer differs\n", line);
        print_hex("got", g_pkt, n);
        print_hex("expected", want, n);
    }
}

#define EXPECT_PKT(text) expect_pkt(text, __LINE__)

static void expect(const char *text, int line)
{
    unsigned char want[4096];
    unsigned int n = hex(want, sizeof(want), text);

    if (g_t->tx_len != n || memcmp(g_t->tx, want, n) != 0) {
        t_failures++;
        printf("t_proto.c(%d): the server's bytes differ (end: %s)\n", line,
               g_t->ending ? g_t->why : "no");
        print_hex("got", g_t->tx, g_t->tx_len);
        print_hex("expected", want, n);
    }
    g_t->tx_len = 0;
}

#define EXPECT(text) expect(text, __LINE__)

static void expect_all(int line, ...)
{
    static char all[16384];
    const char *p;
    va_list ap;

    all[0] = '\0';
    va_start(ap, line);
    while ((p = va_arg(ap, const char *)) != NULL) {
        strcat(all, p);
    }
    va_end(ap);
    expect(all, line);
}

#define EXPECT2(a, b) expect_all(__LINE__, a, b, NULL)

#define CC_PLAIN "03 00 00 0B 06 D0 00 00 12 34 00"
#define CC_NEG   "03 00 00 13 0E D0 00 00 12 34 00 02 00 08 00 00 00 00 00"
#define CR_PLAIN "03 00 00 0B 06 E0 00 00 00 00 00"

static void test_x224(void)
{
    reset();
    feed(CR_PLAIN);
    EXPECT(CC_PLAIN);
    CHECK_EQ(g_s.state, ST_WAIT_CI);
    CHECK_EQ(g_s.ending, END_NO);

    reset();
    feed("03 00 00 23 1E E0 00 00 00 00 00 43 6F 6F 6B 69 65 3A 20 6D 73 74"
         "73 68 61 73 68 3D 74 65 6E 6F 78 0D 0A");
    EXPECT(CC_PLAIN);
    CHECK_EQ(g_s.state, ST_WAIT_CI);

    reset();
    feed("03 00 00 2B 26 E0 00 00 00 00 00 43 6F 6F 6B 69 65 3A 20 6D 73 74"
         "73 68 61 73 68 3D 74 65 6E 6F 78 0D 0A 01 00 08 00 01 00 00 00");
    EXPECT(CC_NEG);
    CHECK_EQ(g_s.state, ST_WAIT_CI);

    reset();
    feed("03 00 00 13 0E E0 00 00 00 00 00 01 00 08 00 03 00 00 00");
    EXPECT(CC_NEG);

    reset();
    feed("03 00 00 1C 17 E0 00 00 00 00 00 43 6F 6F 6B 69 65 3A 20 78"
         "01 00 08 00 01 00 00 00");
    EXPECT(CC_PLAIN);

    reset();
    feed("03 00 00 12 0D E0 00 00 00 00 00 01 00 08 00 01 00 00");
    EXPECT(CC_PLAIN);

    reset();
    feed("03 00 00 0B 06 E0 00 00 56 78 00");
    EXPECT("03 00 00 0B 06 D0 56 78 12 34 00");

    reset();
    feed("03 00 00 0C 02 F0 80 04 01 00 01 00");
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NOW);
    CHECK_EQ(g_s.state, ST_WAIT_CR);
    reset();
    feed("03 00 00 07 02 E0 00");
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NOW);

    reset();
    feed("08 06 00 1E 01 1E");
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NO);
    CHECK_EQ(g_s.fastpath, 1);
    CHECK_EQ(g_s.state, ST_WAIT_CR);

    reset();
    feed(CR_PLAIN);
    EXPECT(CC_PLAIN);
    feed("03 00 00 0B 06 80 00 00 00 00 00");
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NOW);

    reset();
    feed(CR_PLAIN);
    EXPECT(CC_PLAIN);
    feed(CR_PLAIN);
    CHECK_EQ(g_s.ending, END_NOW);
    reset();
    feed(CR_PLAIN);
    EXPECT(CC_PLAIN);
    feed("03 00 00 08 09 F0 80 00");
    CHECK_EQ(g_s.ending, END_NOW);
}

static void connected(void)
{
    reset();
    feed(CR_PLAIN);
    EXPECT(CC_PLAIN);
}

static void test_connect(void)
{
    connected();
    feed(CI_210);
    EXPECT(CRESP_0);
    CHECK_EQ(g_s.state, ST_WAIT_DOMAIN);
    CHECK_EQ(g_s.core_len, 210);
    CHECK_EQ(g_s.client_version, 0x00080004UL);
    CHECK_EQ(g_s.client_width, 1024);
    CHECK_EQ(g_s.client_height, 768);
    CHECK_EQ(g_s.high_depth, 16);
    CHECK_EQ(g_s.supported_depths, 0x0007);
    CHECK_EQ(g_s.early_flags, 0x0001);
    CHECK_EQ(g_s.kbd_layout, 0x409);
    CHECK_EQ(g_s.kbd_type, 4);
    CHECK_EQ(g_s.kbd_subtype, 0);
    CHECK_EQ(g_s.kbd_fkeys, 12);
    CHECK_EQ(g_s.channels, 0);
    CHECK_EQ(g_s.depth, 8);
    CHECK_EQ(g_s.ending, END_NO);

    connected();
    feed(CI_136);
    EXPECT(CRESP_0);
    CHECK_EQ(g_s.core_len, 136);
    CHECK_EQ(g_s.client_version, 0x00080001UL);
    CHECK_EQ(g_s.high_depth, 0);
    CHECK_EQ(g_s.early_flags, 0);
    CHECK_EQ(g_s.state, ST_WAIT_DOMAIN);

    connected();
    feed(CI_140);
    EXPECT(CRESP_0);
    CHECK_EQ(g_s.core_len, 140);
    CHECK_EQ(g_s.high_depth, 0);

    connected();
    feed(CI_216_3);
    EXPECT(CRESP_3);
    CHECK_EQ(g_s.core_len, 216);
    CHECK_EQ(g_s.high_depth, 24);
    CHECK_EQ(g_s.channels, 3);

    connected();
    feed(CI_1);
    EXPECT(CRESP_1);
    CHECK_EQ(g_s.channels, 1);
    connected();
    feed(CI_31);
    EXPECT(CRESP_31);
    CHECK_EQ(g_s.channels, 31);
    CHECK_EQ(g_s.state, ST_WAIT_DOMAIN);

    connected();
    feed(CI_43);
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NOW);

    connected();
    feed("03 00 00 0C 02 F0 80 04 01 00 01 00");
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NOW);

    connected();
    feed("03 00 00 11 02 F0 80 7F 65 82 01 61 04 01 01 04 01");
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NOW);
}

#define EDRQ_MSTSC    "03 00 00 0C 02 F0 80 04 01 00 01 00"
#define EDRQ_RDESKTOP "03 00 00 0C 02 F0 80 04 00 01 00 01"
#define AURQ          "03 00 00 08 02 F0 80 28"
#define DPUM_USER     "03 00 00 09 02 F0 80 21 80"

static void join(unsigned int n, unsigned int ch)
{
    char text[80];
    unsigned int u = 1004 + n - 1001;

    sprintf(text, "03 00 00 0C 02 F0 80 38 %02X %02X %02X %02X",
            u >> 8, u & 0xFF, ch >> 8, ch & 0xFF);
    feed(text);
    sprintf(text, "03 00 00 0F 02 F0 80 3E 00 %02X %02X %02X %02X %02X %02X",
            u >> 8, u & 0xFF, ch >> 8, ch & 0xFF, ch >> 8, ch & 0xFF);
    EXPECT(text);
}

static void test_domain(void)
{
    connected();
    feed(CI_210);
    EXPECT(CRESP_0);
    feed(EDRQ_MSTSC);
    EXPECT("");
    feed(EDRQ_RDESKTOP);
    EXPECT("");
    CHECK_EQ(g_s.state, ST_WAIT_DOMAIN);
    feed(AURQ);
    EXPECT("03 00 00 0B 02 F0 80 2E 00 00 03");
    CHECK_EQ(g_s.state, ST_WAIT_JOINS);
    CHECK_EQ(g_s.user_id, 1004);
    feed("03 00 00 0C 02 F0 80 38 00 03 03 EC");
    EXPECT("03 00 00 0F 02 F0 80 3E 00 00 03 03 EC 03 EC");
    feed("03 00 00 0C 02 F0 80 38 00 03 03 EB");
    EXPECT("03 00 00 0F 02 F0 80 3E 00 00 03 03 EB 03 EB");
    CHECK_EQ(g_s.state, ST_WAIT_JOINS);
    CHECK_EQ(g_s.ending, END_NO);

    feed(AURQ);
    EXPECT("");
    feed(EDRQ_MSTSC);
    EXPECT("");
    feed("03 00 00 08 02 F0 80 2C");
    EXPECT("");
    feed("03 00 00 09 02 F0 80 FC 00");
    EXPECT("");
    CHECK_EQ(g_s.state, ST_WAIT_JOINS);
    CHECK_EQ(g_s.ending, END_NO);

    feed(DPUM_USER);
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NOW);
    CHECK(strstr(g_s.why, "reason 3 (user requested)") != NULL);

    connected();
    feed(CI_216_3);
    EXPECT(CRESP_3);
    feed(EDRQ_RDESKTOP);
    EXPECT("");
    feed(AURQ);
    EXPECT("03 00 00 0B 02 F0 80 2E 00 00 06");
    CHECK_EQ(g_s.user_id, 1007);
    join(3, 1007);
    join(3, 1003);
    join(3, 1004);
    join(3, 1005);
    join(3, 1006);
    join(3, 2000);
    CHECK_EQ(g_s.state, ST_WAIT_JOINS);
    CHECK_EQ(g_s.ending, END_NO);

    connected();
    feed(CI_31);
    EXPECT(CRESP_31);
    feed(AURQ);
    EXPECT("03 00 00 0B 02 F0 80 2E 00 00 22");
    join(31, 1035);
    join(31, 1034);

    connected();
    feed(CI_210);
    EXPECT(CRESP_0);
    feed("03 00 00 0C 02 F0 80 38 00 03 03 EB");
    EXPECT("");
    CHECK_EQ(g_s.state, ST_WAIT_DOMAIN);
    CHECK_EQ(g_s.ending, END_NO);

    feed(AURQ);
    EXPECT("03 00 00 0B 02 F0 80 2E 00 00 03");
    feed("03 00 00 0A 02 F0 80 38 00 03");
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NOW);

    connected();
    feed(CI_210);
    EXPECT(CRESP_0);
    feed("03 00 00 07 02 F0 80");
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NOW);

    connected();
    feed(DPUM_USER);
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NOW);
    CHECK(strstr(g_s.why, "Ultimatum, reason 3") != NULL);
    connected();
    feed(CI_210);
    EXPECT(CRESP_0);
    feed("03 00 00 09 02 F0 80 22 00");
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NOW);
    CHECK(strstr(g_s.why, "reason 4 (channel purged)") != NULL);
}

static const char *g_ci = CI_210;

static void joined(void)
{
    connected();
    feed(g_ci);
    EXPECT(CRESP_0);
    feed(EDRQ_MSTSC);
    feed(AURQ);
    EXPECT("03 00 00 0B 02 F0 80 2E 00 00 03");
    join(0, 1004);
    join(0, 1003);
}

#define INFO_PW_UNI \
    "03 00 00 3C 02 F0 80 64 00 03 03 EB 70 2E 40 00 00 00 00 00 00 00" \
    "3B 01 00 00 00 00 0A 00 04 00 00 00 00 00 00 00 74 00 65 00 6E 00" \
    "6F 00 78 00 00 00 70 00 77 00 00 00 00 00 00 00"
#define INFO_PW_UNI_HIDDEN \
    "03 00 00 3C 02 F0 80 64 00 03 03 EB 70 2E 40 00 00 00 00 00 00 00" \
    "3B 01 00 00 00 00 0A 00 04 00 00 00 00 00 00 00 74 00 65 00 6E 00" \
    "6F 00 78 00 00 00 00 00 00 00 00 00 00 00 00 00"
#define INFO_PW_ANSI \
    "03 00 00 30 02 F0 80 64 00 03 03 EB 70 22 40 00 00 00 00 00 00 00" \
    "2B 01 00 00 00 00 05 00 02 00 00 00 00 00 00 74 65 6E 6F 78 00 70" \
    "77 00 00 00"
#define INFO_PW_ANSI_HIDDEN \
    "03 00 00 30 02 F0 80 64 00 03 03 EB 70 22 40 00 00 00 00 00 00 00" \
    "2B 01 00 00 00 00 05 00 02 00 00 00 00 00 00 74 65 6E 6F 78 00 00" \
    "00 00 00 00"

static void test_info(void)
{
    joined();
    feed(INFO_6_2);
    EXPECT2(LICENSE, DEMAND_1024);
    CHECK_EQ(g_s.licensed, 1);
    CHECK_EQ(g_s.ending, END_NO);
    CHECK_EQ(g_s.state, ST_WAIT_CONFIRM);
    CHECK(strcmp(g_s.user, "tenox") == 0);
    CHECK_EQ(g_s.info_flags, 0x133);
    feed(INFO_6_2);
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NO);
    CHECK_EQ(g_s.state, ST_WAIT_CONFIRM);
    feed("03 00 00 22 02 F0 80 64 00 03 03 EB 70 14 80 00 00 00 FF 03 10 00"
         "07 00 00 00 02 00 00 00 04 00 00 00");
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NO);

    joined();
    feed(INFO_MSTSC);
    EXPECT2(LICENSE, DEMAND_1024);
    CHECK_EQ(g_s.state, ST_WAIT_CONFIRM);
    CHECK(strcmp(g_s.user, "rdpcheck") == 0);
    CHECK_EQ(g_s.info_flags, 0x4173);

    joined();
    feed("03 00 00 2E 02 F0 80 64 00 03 03 EB 70 20 40 00 00 00 00 00 00 00"
         "23 01 00 00 00 00 05 00 00 00 00 00 00 00 00 74 65 6E 6F 78 00 00"
         "00 00");
    EXPECT2(LICENSE, DEMAND_1024);
    CHECK_EQ(g_s.state, ST_WAIT_CONFIRM);
    CHECK(strcmp(g_s.user, "tenox") == 0);
    CHECK_EQ(g_s.info_flags, 0x123);

    joined();
    feed(INFO_PW_UNI);
    EXPECT_PKT(INFO_PW_UNI_HIDDEN);
    EXPECT2(LICENSE, DEMAND_1024);
    CHECK(strcmp(g_s.user, "tenox") == 0);
    CHECK_EQ(g_s.state, ST_WAIT_CONFIRM);
    feed(INFO_PW_UNI);
    EXPECT_PKT(INFO_PW_UNI);
    EXPECT("");
    joined();
    feed(INFO_PW_ANSI);
    EXPECT_PKT(INFO_PW_ANSI_HIDDEN);
    EXPECT2(LICENSE, DEMAND_1024);
    CHECK(strcmp(g_s.user, "tenox") == 0);

    joined();
    feed("03 00 00 3C 02 F0 80 64 00 03 03 EB 70 2E 40 00 00 00 00 00 00 00"
         "3B 01 00 00 00 00 0A 00 40 00 00 00 00 00 00 00 74 00 65 00 6E 00"
         "6F 00 78 00 00 00 70 00 77 00 00 00 00 00 00 00");
    EXPECT_PKT("03 00 00 3C 02 F0 80 64 00 03 03 EB 70 2E 40 00 00 00 00 00"
               "00 00 3B 01 00 00 00 00 0A 00 40 00 00 00 00 00 00 00 74 00"
               "65 00 6E 00 6F 00 78 00 00 00 00 00 00 00 00 00 00 00 00 00");
    EXPECT2(LICENSE, DEMAND_1024);
    CHECK_EQ(g_s.ending, END_NO);
    CHECK_EQ(g_s.state, ST_WAIT_CONFIRM);
    CHECK_EQ(g_s.user[0], 0);

    joined();
    feed("03 00 00 38 02 F0 80 64 00 03 03 EB 50 2A 40 00 00 00 00 00 00 00"
         "33 01 00 00 00 00 0A 00 00 00 00 00 00 00 00 00 74 00 65 00 6E 00"
         "6F 00 78 00 00 00 00 00 00 00 00 00");
    EXPECT2(LICENSE, DEMAND_1024);
    CHECK_EQ(g_s.state, ST_WAIT_CONFIRM);

    joined();
    feed("03 00 00 1E 02 F0 80 64 00 03 03 EB 70 10 01 00 00 00 08 00 00 00"
         "11 22 33 44 55 66 77 88");
    EXPECT(DPUM_1);
    CHECK_EQ(g_s.ending, END_FLUSH);
    CHECK_EQ(g_s.licensed, 0);
    CHECK(strstr(g_s.why, "encryption") != NULL);

    joined();
    feed("03 00 00 3C 02 F0 80 64 00 03 03 EB 70 2E 48 00 00 00 00 00 00 00"
         "3B 01 00 00 00 00 0A 00 04 00 00 00 00 00 00 00 74 00 65 00 6E 00"
         "6F 00 78 00 00 00 70 00 77 00 00 00 00 00 00 00");
    EXPECT_PKT("03 00 00 3C 02 F0 80 64 00 03 03 EB 70 2E 48 00 00 00 00 00"
               "00 00 3B 01 00 00 00 00 0A 00 04 00 00 00 00 00 00 00 74 00"
               "65 00 6E 00 6F 00 78 00 00 00 00 00 00 00 00 00 00 00 00 00");
    EXPECT2(LICENSE, DEMAND_1024);
    CHECK_EQ(g_s.ending, END_NO);
    CHECK_EQ(g_s.state, ST_WAIT_CONFIRM);
    CHECK(strcmp(g_s.user, "tenox") == 0);
    CHECK_EQ(g_s.info_flags, 0x13B);

    joined();
    feed("03 00 00 16 02 F0 80 64 00 03 03 EB 70 08 00 00 00 00 11 22 33 44");
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NOW);

    joined();
    feed("03 00 00 10 02 F0 80 64 00 03 03 EB 70 02 40 00");
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NOW);
    joined();
    feed("03 00 00 1C 02 F0 80 64 00 03 03 EB 70 0E 40 00 00 00 00 00 00 00"
         "00 00 00 00 00 00");
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NOW);
    joined();
    feed("03 00 00 10 02 F0 80 64 00 03 03 EB 70 05 40 00");
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NOW);

    connected();
    feed(CI_216_3);
    EXPECT(CRESP_3);
    feed(AURQ);
    EXPECT("03 00 00 0B 02 F0 80 2E 00 00 06");
    feed("03 00 00 12 02 F0 80 64 00 06 03 EC 70 04 40 00 00 00");
    EXPECT("");
    CHECK_EQ(g_s.state, ST_WAIT_JOINS);
    CHECK_EQ(g_s.ending, END_NO);
    feed(INFO_6_2);
    EXPECT2(LICENSE, DEMAND_1024);
    CHECK_EQ(g_s.state, ST_WAIT_CONFIRM);

    connected();
    feed(CI_210);
    EXPECT(CRESP_0);
    feed(INFO_PW_UNI);
    EXPECT("");
    EXPECT_PKT(INFO_PW_UNI_HIDDEN);
    CHECK_EQ(g_s.state, ST_WAIT_DOMAIN);
    CHECK_EQ(g_s.ending, END_NO);
}

static void licensed(void)
{
    joined();
    feed(INFO_MSTSC);
    EXPECT2(LICENSE, DEMAND_1024);
    CHECK_EQ(g_t->state, ST_WAIT_CONFIRM);
    CHECK_EQ(g_t->share_id, 0x000103EAUL);
}

static void test_finalize(void)
{
    CLIENT_CAPS *c = &g_s.caps;

    licensed();
    feed(C_SYNC);
    EXPECT("");
    feed(CONFIRM_MSTSC);
    EXPECT("");
    CHECK_EQ(g_s.state, ST_FINALIZE);
    CHECK_EQ(c->extra_flags, 0x0400);
    CHECK_EQ(c->ask_bpp, 16);
    CHECK_EQ(c->ask_width, 1024);
    CHECK_EQ(c->ask_height, 768);
    CHECK_EQ(c->bitmap_compression, 1);
    CHECK_EQ(c->order_flags, 0x0022);
    CHECK_EQ(c->color_pointer, 1);
    CHECK_EQ(c->color_pointer_cache, 20);
    CHECK_EQ(c->pointer_cache, 21);
    CHECK_EQ(c->color_table_cache, 6);
    CHECK_EQ(c->input_flags, 0x0001);
    CHECK_EQ(c->input_kbd_layout, 0x409);
    CHECK_EQ(c->have, (1UL << 1) | (1UL << 2) | (1UL << 3) | (1UL << 4) |
             (1UL << 5) | (1UL << 7) | (1UL << 8) | (1UL << 9) | (1UL << 10) |
             (1UL << 12) | (1UL << 13) | (1UL << 14) | (1UL << 15) |
             (1UL << 16));
    feed(C_SYNC);
    EXPECT(SYNC);
    feed(C_COOP);
    EXPECT(COOP);
    feed(C_REQCTL);
    EXPECT(GRANTED);
    CHECK_EQ(g_s.state, ST_FINALIZE);
    feed(C_FONT3);
    EXPECT(FONTMAP);
    CHECK_EQ(g_s.state, ST_ACTIVE);
    CHECK_EQ(g_s.ending, END_NO);
    feed(CONFIRM_MSTSC);
    feed(C_FONT3);
    feed(C_SYNC);
    EXPECT("");
    CHECK_EQ(g_s.state, ST_ACTIVE);
    CHECK_EQ(g_s.ending, END_NO);

    licensed();
    feed(CONFIRM_RDP4);
    CHECK_EQ(c->extra_flags, 0);
    CHECK_EQ(c->color_pointer, 0);
    CHECK_EQ(c->color_pointer_cache, 20);
    CHECK_EQ(c->pointer_cache, 0);
    feed(C_SYNC);
    feed(C_COOP);
    feed(C_REQCTL);
    expect_all(__LINE__, SYNC, COOP, GRANTED, NULL);
    feed(C_INSYNC);
    feed(C_FONT1);
    EXPECT("");
    CHECK_EQ(g_s.state, ST_FINALIZE);
    feed(C_FONT2);
    EXPECT(FONTMAP);
    CHECK_EQ(g_s.state, ST_ACTIVE);

    licensed();
    feed(CONFIRM_MSTSC);
    feed(C_FOUR);
    expect_all(__LINE__, SYNC, COOP, GRANTED, FONTMAP, NULL);
    CHECK_EQ(g_s.state, ST_ACTIVE);

    licensed();
    feed(CONFIRM_MSTSC);
    feed("03 00 00 16 02 F0 80 64 00 03 03 EB 70 08 00 80 41 00 00 01 EC 03");
    feed("03 00 00 14 02 F0 80 64 00 03 03 EB 70 06 06 00 11 00 EC 03");
    feed("03 00 00 20 02 F0 80 64 00 03 03 EB 70 12 12 00 17 00 EC 03 EA 03"
         "01 00 00 01 04 00 7F 00 00 00");
    feed("03 00 00 20 02 F0 80 64 00 03 03 EB 70 12 12 00 17 00 EC 03 EA 03"
         "01 00 00 01 04 00 2B 00 00 00");
    EXPECT("");
    CHECK(g_s.t2_logged[0x7F >> 3] & (1 << (0x7F & 7)));
    feed(C_SYNC);
    EXPECT(SYNC);
    CHECK_EQ(g_s.ending, END_NO);

    feed("03 00 00 14 02 F0 80 64 00 03 03 EB 70 06 05 00 17 00 EC 03");
    feed("03 00 00 14 02 F0 80 64 00 03 03 EB 70 06 40 00 17 00 EC 03");
    feed("03 00 00 14 02 F0 80 64 00 03 03 EB 70 06 06 00 17 00 EC 03");
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NO);
    CHECK_EQ(g_s.state, ST_FINALIZE);

    licensed();
    feed("03 00 00 32 02 F0 80 64 00 03 03 EB 70 24 24 00 13 00 EC 03 EA 03"
         "09 00 EA 03 04 00 10 00 41 42 43 00 02 00 00 00 0A 00 08 00 06 00"
         "00 00 01 00 03 00");
    EXPECT("");
    CHECK_EQ(g_s.state, ST_FINALIZE);
    CHECK_EQ(c->color_table_cache, 6);
    CHECK_EQ(c->have, 1UL << 10);
    CHECK_EQ(g_s.ending, END_NO);
}

static void sdin_case(unsigned int n, const char *head)
{
    static unsigned char buf[MCS_SDIN_MAX + 64];
    unsigned char want[16];
    unsigned int h = hex(want, sizeof(want), head);
    unsigned int i;
    OUTS o;

    reset();
    mcs_sdin_begin(&o, buf, sizeof(buf));
    for (i = 0; i < n; i++) {
        out_u8(&o, i * 7 + 1);
    }
    CHECK(mcs_sdin_end(&g_s, &o));
    CHECK_EQ(g_s.tx_len, h + n);
    CHECK_MEM(g_s.tx, want, h);
    for (i = 0; i < n && g_s.tx[h + i] == (unsigned char) (i * 7 + 1); i++) {
    }
    CHECK_EQ(i, n);
}

static void test_sdin(void)
{
    static unsigned char buf[MCS_SDIN_MAX + 64];
    OUTS o;

    sdin_case(0, "03 00 00 0E 02 F0 80 68 00 01 03 EB 70 00");
    sdin_case(127, "03 00 00 8D 02 F0 80 68 00 01 03 EB 70 7F");
    sdin_case(128, "03 00 00 8F 02 F0 80 68 00 01 03 EB 70 80 80");
    sdin_case(MCS_SDIN_MAX, "03 00 40 0E 02 F0 80 68 00 01 03 EB 70 BF FF");

    reset();
    mcs_sdin_begin(&o, buf, sizeof(buf));
    out_zero(&o, MCS_SDIN_MAX + 1);
    CHECK(!mcs_sdin_end(&g_s, &o));
    CHECK_EQ(g_s.tx_len, 0);
    CHECK_EQ(g_s.ending, END_NOW);
}

static void send_body(unsigned int type2, const unsigned char *body,
                      unsigned int n)
{
    static unsigned char buf[600];
    OUTS o;

    share_begin(&g_s, &o, buf, sizeof(buf));
    out_bytes(&o, body, n);
    CHECK(share_end(&g_s, &o, type2));
}

static void test_compress(void)
{
    unsigned char body[86];
    static const char pdu_e1[] =
        "03 00 00 36 02 F0 80 68 00 01 03 EB 70 28"
        "28 00 17 00 EA 03 EA 03 01 00 00 02 68 00 02 E1 28 00"
        "01 00 01 00 F8 30 1E 00 06 00 20 00 08 00 11 F3 48 00 00 FF 07 DF";

    reset();
    g_s.share_id = 0x000103EAUL;
    g_s.state = ST_ACTIVE;
    share_start_compression(&g_s, 3);
    CHECK(g_s.mppc != NULL);
    if (g_s.mppc == NULL) {
        return;
    }
    CHECK_EQ(g_s.mppc->type, 1);
    hex(body, sizeof(body), "01 00 01 00 00 00 00 00 0F 00 03 00 10 00 04 00 "
        "08 00 00 00 40 00");
    memset(body + 22, 7, 64);
    send_body(T2_UPDATE, body, 86);
    EXPECT(pdu_e1);
    send_body(T2_UPDATE, body, 86);
    EXPECT("03 00 00 24 02 F0 80 68 00 01 03 EB 70 16"
           "16 00 17 00 EA 03 EA 03 01 00 00 02 68 00 02 21 16 00"
           "F0 B7 CB 00");
    send_body(T2_CONTROL, body, 86);
    CHECK_EQ(g_s.tx_len, 14 + 18 + 86);
    CHECK_EQ(g_s.tx[14 + 15], 0);
    CHECK_EQ(g_s.tx[14 + 16] | g_s.tx[14 + 17], 0);
    g_s.tx_len = 0;
    send_body(T2_POINTER, body, 32);
    CHECK_EQ(g_s.tx_len, 14 + 18 + 32);
    CHECK_EQ(g_s.tx[14 + 15], 0);
    g_s.tx_len = 0;
    send_body(T2_POINTER, (const unsigned char *) "\x81\x92\xA3\xB4\xC5\xD6"
              "\xE7\xF8\x09\x1A\x2B\x3C\x4D\x5E\x6F\x70\x81\x93\xA5\xB7"
              "\xC9\xDB\xED\xFF\x11\x23\x35\x47\x59\x6B\x7D\x8F\x91\xA2",
              34);
    CHECK_EQ(g_s.tx_len, 14 + 18 + 34);
    CHECK_EQ(g_s.tx[14 + 15], 0);
    CHECK_EQ(g_s.mppc->expanded, 1);
    g_s.tx_len = 0;
    share_restart_compression(&g_s);
    send_body(T2_UPDATE, body, 86);
    EXPECT(pdu_e1);
    g_s.state = ST_FINALIZE;
    send_body(T2_UPDATE, body, 86);
    CHECK_EQ(g_s.tx[14 + 15], 0);
    g_s.tx_len = 0;
    g_s.state = ST_ACTIVE;
    CHECK_EQ(g_s.mppc->packed, 3);
    share_done(&g_s);
    CHECK(g_s.mppc == NULL);
}

static void test_depth(void)
{
    SESSION *s = &g_s;

    reset();
    s->core_len = 136;
    CHECK_EQ(gcc_session_depth(s, 16), 8);
    CHECK_EQ(gcc_session_depth(s, 24), 8);

    s->core_len = 210;
    s->high_depth = 16;
    s->supported_depths = 0x0007;
    CHECK_EQ(gcc_session_depth(s, 16), 16);
    CHECK_EQ(gcc_session_depth(s, 15), 15);
    CHECK_EQ(gcc_session_depth(s, 24), 24);
    CHECK_EQ(gcc_session_depth(s, 32), 24);
    CHECK_EQ(gcc_session_depth(s, 8), 8);
    CHECK_EQ(gcc_session_depth(s, 4), 8);

    s->core_len = 216;
    s->supported_depths = 0x000B;
    CHECK_EQ(gcc_session_depth(s, 15), 8);
    s->high_depth = 15;
    CHECK_EQ(gcc_session_depth(s, 15), 15);

    s->supported_depths = 0;
    s->high_depth = 24;
    CHECK_EQ(gcc_session_depth(s, 24), 24);
    CHECK_EQ(gcc_session_depth(s, 16), 8);
}

static unsigned char g_screen[768][1024];
static unsigned char g_seen[768][1024];
static unsigned long g_crc;
static unsigned long g_bytes;
static unsigned long g_rle;
static unsigned int  g_last[4];

static unsigned long crc32_add(unsigned long crc, const unsigned char *p,
                               unsigned long n)
{
    int k;

    while (n-- > 0) {
        crc ^= *p++;
        for (k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
        }
    }
    return crc;
}

static void clear_screen(void)
{
    memset(g_screen, 0xEE, sizeof(g_screen));
    memset(g_seen, 0, sizeof(g_seen));
    g_crc = 0xFFFFFFFFUL;
    g_bytes = 0;
    g_rle = 0;
}

static unsigned long seen_count(void)
{
    unsigned long n = 0;
    unsigned int x, y;

    for (y = 0; y < 768; y++) {
        for (x = 0; x < 1024; x++) {
            n += g_seen[y][x];
        }
    }
    return n;
}

static unsigned long picture_crc(void)
{
    return crc32_add(0xFFFFFFFFUL, &g_screen[0][0], sizeof(g_screen)) ^
           0xFFFFFFFFUL;
}

static unsigned int get16(const unsigned char *p)
{
    return p[0] | (p[1] << 8);
}

static unsigned int take_bitmaps(void)
{
    static unsigned char ms[64 * 64], rd[64 * 64];
    unsigned int pos = 0, packets = 0;
    unsigned int len, at, n, i, x, y, w, h, r, size, off;
    const unsigned char *p, *pix;

    while (pos + 16 <= g_s.tx_len) {
        p = g_s.tx + pos;
        len = (p[2] << 8) | p[3];
        CHECK(len <= 8192);
        if (len < 16 || pos + len > g_s.tx_len) {
            break;
        }
        at = (p[13] & 0x80) ? 15 : 14;
        CHECK_EQ(p[at + 14], 2);
        CHECK_EQ(get16(p + at + 18), 1);
        n = get16(p + at + 20);
        CHECK(n >= 1);
        at += 22;
        for (i = 0; i < n && at + 18 <= len; i++) {
            x = get16(p + at);
            y = get16(p + at + 2);
            w = get16(p + at + 8);
            h = get16(p + at + 10);
            size = get16(p + at + 16);
            CHECK_EQ(get16(p + at + 4), x + w - 1);
            CHECK_EQ(get16(p + at + 6), y + h - 1);
            CHECK_EQ(w % 4, 0);
            CHECK(h >= 1 && x + w <= 1024 && y + h <= 768);
            CHECK(w <= 64 && h <= 64);
            CHECK_EQ(get16(p + at + 12), 8);
            at += 18;
            if (x + w > 1024 || y + h > 768 || w > 64 || h > 64 ||
                at + size > len) {
                break;
            }
            if (get16(p + at - 4) == 0) {
                CHECK_EQ(size, w * h);
                pix = p + at;
            } else {
                if (g_s.caps.extra_flags & 0x0400) {
                    CHECK_EQ(get16(p + at - 4), 0x0401);
                    off = 0;
                } else {
                    CHECK_EQ(get16(p + at - 4), 1);
                    CHECK(size > 8);
                    CHECK_EQ(get16(p + at), 0);
                    CHECK_EQ(get16(p + at + 2), size - 8);
                    CHECK_EQ(get16(p + at + 4), w);
                    CHECK_EQ(get16(p + at + 6), w * h);
                    off = 8;
                }
                CHECK(size > off && size < w * h);
                memset(ms, 0xEE, sizeof(ms));
                memset(rd, 0xDD, sizeof(rd));
                CHECK(rledec(p + at + off, (long) (size - off), (int) w,
                             (int) h, 8, RLEDEC_MS, ms));
                CHECK(rledec(p + at + off, (long) (size - off), (int) w,
                             (int) h, 8, RLEDEC_RD, rd));
                CHECK_MEM(ms, rd, w * h);
                pix = ms;
                g_rle++;
            }
            for (r = 0; r < h; r++) {
                memcpy(&g_screen[y + h - 1 - r][x], pix + r * w, w);
                memset(&g_seen[y + h - 1 - r][x], 1, w);
            }
            g_last[0] = x;
            g_last[1] = y;
            g_last[2] = w;
            g_last[3] = h;
            at += size;
        }
        CHECK_EQ(at, len);
        g_crc = crc32_add(g_crc, p, len);
        g_bytes += len;
        pos += len;
        packets++;
    }
    CHECK_EQ(pos, g_s.tx_len);
    g_s.tx_len = 0;
    return packets;
}

static unsigned int drain(void)
{
    unsigned int packets = 0, rounds;

    for (rounds = 0; rounds < 1000; rounds++) {
        update_pump(&g_s);
        if (g_s.tx_len == 0) {
            break;
        }
        CHECK(g_s.tx_len < UPDATE_QUEUE_MAX + 8192);
        packets += take_bitmaps();
    }
    return packets;
}

static void take(const char *text, int line)
{
    static unsigned char want[4096];
    unsigned int n = hex(want, sizeof(want), text);

    if (g_t->tx_len < n || memcmp(g_t->tx, want, n) != 0) {
        t_failures++;
        printf("t_proto.c(%d): the server's first bytes differ\n", line);
        print_hex("got", g_t->tx, g_t->tx_len < n ? g_t->tx_len : n);
        print_hex("expected", want, n);
        return;
    }
    memmove(g_t->tx, g_t->tx + n, g_t->tx_len - n);
    g_t->tx_len -= n;
}

#define TAKE(text) take(text, __LINE__)

static void active(void)
{
    licensed();
    feed(CONFIRM_MSTSC);
    feed(C_FOUR);
    expect_all(__LINE__, SYNC, COOP, GRANTED, FONTMAP, NULL);
    CHECK_EQ(g_t->state, ST_ACTIVE);
    pointer_pump(g_t);
    TAKE(PTR_DEFAULT);
}

static void set_rect(RECT *r, long left, long top, long right,
                     long bottom)
{
    r->left = left;
    r->top = top;
    r->right = right;
    r->bottom = bottom;
}

static void test_screen(void)
{
    unsigned char want[128];
    unsigned int n;
    RECT r[2];

    active();
    CHECK(g_s.upd_palette);
    update_pump(&g_s);
    TAKE(PALETTE);
    clear_screen();
    n = take_bitmaps();
    n += drain();
    CHECK_EQ(n, SCREEN_PDUS);
    CHECK_EQ(g_bytes, SCREEN_BYTES);
    CHECK_EQ(g_crc ^ 0xFFFFFFFFUL, SCREEN_CRC);
    CHECK_EQ(g_rle, SCREEN_RLE);
    CHECK_EQ(seen_count(), 1024UL * 768UL);
    CHECK_EQ(picture_crc(), PICTURE_CRC);
    CHECK_EQ(g_s.map.dirty, 0);
    CHECK(!g_s.tile.ready);
    CHECK_EQ(g_s.stats.tiles_rle, SCREEN_RLE);
    CHECK_EQ(g_s.stats.tiles_raw, 192 - SCREEN_RLE);
    CHECK_EQ(g_s.stats.pixel_bytes, 1024UL * 768UL);
    CHECK(g_s.stats.first_logged);
    CHECK_EQ(g_s.ending, END_NO);

    licensed();
    feed(CONFIRM_RDP4);
    feed(C_FOUR);
    expect_all(__LINE__, SYNC, COOP, GRANTED, FONTMAP, NULL);
    CHECK_EQ(g_s.caps.extra_flags, 0);
    update_pump(&g_s);
    TAKE(PALETTE);
    clear_screen();
    n = take_bitmaps();
    n += drain();
    CHECK_EQ(n, HDR_SCREEN_PDUS);
    CHECK_EQ(g_bytes, HDR_SCREEN_BYTES);
    CHECK_EQ(g_crc ^ 0xFFFFFFFFUL, HDR_SCREEN_CRC);
    CHECK_EQ(g_rle, HDR_SCREEN_RLE);
    CHECK_EQ(picture_crc(), PICTURE_CRC);
    CHECK_EQ(g_s.ending, END_NO);
    active();
    update_pump(&g_s);
    TAKE(PALETTE);
    drain();

    feed(C_REFRESH_SMALL);
    EXPECT("");
    update_pump(&g_s);
    EXPECT(BM_SMALL);
    feed(C_REFRESH_CENTER);
    update_pump(&g_s);
    EXPECT(BM_CENTER);
    feed(C_REFRESH_TWO);
    update_pump(&g_s);
    EXPECT(BM_TWO);
    feed(C_REFRESH_CLAMP);
    update_pump(&g_s);
    EXPECT(BM_CLAMP);
    feed(C_REFRESH_EDGE);
    update_pump(&g_s);
    EXPECT(BM_EDGE);
    feed(C_REFRESH_SHORT);
    update_pump(&g_s);
    EXPECT(BM_SMALL);
    feed(C_REFRESH_BIG);
    update_pump(&g_s);
    clear_screen();
    CHECK_EQ(take_bitmaps(), BIG_PDUS);
    CHECK_EQ(g_bytes, BIG_BYTES);
    CHECK_EQ(g_crc ^ 0xFFFFFFFFUL, BIG_CRC);
    CHECK_EQ(g_rle, BIG_RLE);

    set_rect(&r[0], 100, 50, 104, 52);
    core_damage(r, 1);
    EXPECT("");
    rdp_pump(&g_s);
    EXPECT(BM_SMALL);
    set_rect(&r[0], 126, 765, 127, 766);
    set_rect(&r[1], 129, 767, 131, 768);
    core_damage(r, 2);
    rdp_pump(&g_s);
    EXPECT(BM_TWO);
    set_rect(&r[0], 5, 5, 5, 9);
    set_rect(&r[1], 0, 0, 130, 70);
    core_damage(r, 2);
    rdp_pump(&g_s);
    clear_screen();
    CHECK_EQ(take_bitmaps(), BIG_PDUS);
    CHECK_EQ(g_bytes, BIG_BYTES);
    CHECK_EQ(g_crc ^ 0xFFFFFFFFUL, BIG_CRC);
    core_damage(r, 0);
    rdp_pump(&g_s);
    EXPECT("");

    n = hex(want, sizeof(want), BM_SMALL);
    feed(C_REFRESH_SMALL);
    g_s.tx_len = UPDATE_QUEUE_MAX;
    update_pump(&g_s);
    CHECK_EQ(g_s.tx_len, UPDATE_QUEUE_MAX);
    g_s.tx_len = UPDATE_QUEUE_MAX - 1;
    update_pump(&g_s);
    CHECK_EQ(g_s.tx_len, UPDATE_QUEUE_MAX - 1 + n);
    CHECK_MEM(g_s.tx + UPDATE_QUEUE_MAX - 1, want, n);
    g_s.tx_len = 0;
    CHECK_EQ(g_s.ending, END_NO);

    active();
    update_pump(&g_s);
    TAKE(PALETTE);
    clear_screen();
    take_bitmaps();
    feed(C_REFRESH_SMALL);
    drain();
    CHECK_EQ(seen_count(), 1024UL * 768UL);
    CHECK_EQ(picture_crc(), PICTURE_CRC);

    active();
    update_pump(&g_s);
    TAKE(PALETTE);
    drain();
    feed(C_REFRESH_SMALL);
    update_pump(&g_s);
    feed(C_SHUTDOWN);
    EXPECT2(BM_SMALL, DPUM_3);
    CHECK_EQ(g_s.ending, END_FLUSH);
    update_pump(&g_s);
    EXPECT("");

    licensed();
    feed(CONFIRM_MSTSC);
    feed(C_REFRESH_SMALL);
    update_pump(&g_s);
    EXPECT("");
    CHECK_EQ(g_s.map.dirty, 0);
    CHECK(!g_s.upd_palette);
    feed(C_SHUTDOWN);
    EXPECT(DPUM_3);
    CHECK_EQ(g_s.ending, END_FLUSH);
}

static void active_raw(void)
{
    licensed();
    feed(CONFIRM_RAW);
    feed(C_FOUR);
    expect_all(__LINE__, SYNC, COOP, GRANTED, FONTMAP, NULL);
    CHECK_EQ(g_s.state, ST_ACTIVE);
    CHECK_EQ(g_s.caps.bitmap_compression, 0);
}

static void unskip(unsigned int skip)
{
    unsigned int n = g_s.tx_len - skip;

    memmove(g_s.tx, g_s.tx + skip, n);
    g_s.tx_len = n;
}

static void test_raw(void)
{
    unsigned int n;

    active_raw();
    update_pump(&g_s);
    TAKE(PALETTE);
    clear_screen();
    n = take_bitmaps();
    n += drain();
    CHECK_EQ(n, RAW_SCREEN_PDUS);
    CHECK_EQ(g_bytes, RAW_SCREEN_BYTES);
    CHECK_EQ(g_crc ^ 0xFFFFFFFFUL, RAW_SCREEN_CRC);
    CHECK_EQ(g_rle, RAW_SCREEN_RLE);
    CHECK_EQ(seen_count(), 1024UL * 768UL);
    CHECK_EQ(picture_crc(), PICTURE_CRC);
    CHECK_EQ(g_s.stats.tiles_raw, 192);
    CHECK_EQ(g_s.stats.data_bytes, 1024UL * 768UL);

    feed(C_REFRESH_BIG);
    g_s.tx_len = UPDATE_QUEUE_MAX - 1;
    update_pump(&g_s);
    CHECK(g_s.tile.ready);
    CHECK(g_s.tile.x == 64 && g_s.tile.y == 0 && g_s.tile.w == 64 &&
          g_s.tile.h == 64);
    CHECK_EQ(g_s.map.dirty, 4);
    unskip(UPDATE_QUEUE_MAX - 1);
    clear_screen();
    n = take_bitmaps();
    n += drain();
    CHECK_EQ(n, RAW_BIG_PDUS);
    CHECK_EQ(g_bytes, RAW_BIG_BYTES);
    CHECK_EQ(g_crc ^ 0xFFFFFFFFUL, RAW_BIG_CRC);
    CHECK(!g_s.tile.ready);

    update_area(&g_s, 0, 0, 1024, 128);
    g_s.tx_len = UPDATE_QUEUE_MAX - 1;
    update_pump(&g_s);
    unskip(UPDATE_QUEUE_MAX - 1);
    CHECK_EQ(g_s.map.dirty, 30);
    update_area(&g_s, 0, 0, 4, 1);
    CHECK_EQ(g_s.map.dirty, 31);
    clear_screen();
    CHECK_EQ(take_bitmaps() + drain(), 32);
    CHECK(g_last[0] == 0 && g_last[1] == 0 && g_last[2] == 4 &&
          g_last[3] == 1);
    CHECK_EQ(g_s.map.next, 0);
    CHECK_EQ(seen_count(), 1024UL * 128UL);

    active_raw();
    update_pump(&g_s);
    TAKE(PALETTE);
    clear_screen();
    take_bitmaps();
    feed(C_REFRESH_SMALL);
    drain();
    CHECK_EQ(seen_count(), 1024UL * 768UL);
    CHECK_EQ(picture_crc(), PICTURE_CRC);
    CHECK_EQ(g_s.ending, END_NO);
}

static void test_palette_change(void)
{
    active();
    update_pump(&g_s);
    TAKE(PALETTE);
    drain();
    core_frame_changed();
    EXPECT("");
    rdp_pump(&g_s);
    TAKE(PALETTE);
    clear_screen();
    CHECK_EQ(take_bitmaps() + drain(), SCREEN_PDUS);
    CHECK_EQ(g_crc ^ 0xFFFFFFFFUL, SCREEN_CRC);
    CHECK_EQ(picture_crc(), PICTURE_CRC);
    CHECK_EQ(g_s.ending, END_NO);
}

static void take_fixed_palette(const char *head)
{
    static unsigned char want[1024];
    unsigned char pal[256][3];
    unsigned int n = hex(want, sizeof(want), head);

    CHECK_EQ(n, 809);
    reduce_palette(pal);
    memcpy(want + n - sizeof(pal), pal, sizeof(pal));
    CHECK(g_s.tx_len >= n);
    if (g_s.tx_len >= n) {
        CHECK_MEM(g_s.tx, want, n);
        memmove(g_s.tx, g_s.tx + n, g_s.tx_len - n);
        g_s.tx_len -= n;
    }
}

static const unsigned char g_sys[16] = {
    0, 1, 2, 3, 4, 5, 6, 248, 7, 249, 250, 251, 252, 253, 254, 255
};

static void test_reduce(void)
{
    static const unsigned int depths[3] = { 16, 24, 32 };
    static unsigned char want[768][1024];
    unsigned int i, x, y;

    active();
    update_pump(&g_s);
    TAKE(PALETTE);
    clear_screen();
    take_bitmaps();
    drain();
    CHECK_EQ(picture_crc(), PICTURE_CRC);
    CHECK(g_s.reduce == NULL);
    for (y = 0; y < 768; y++) {
        for (x = 0; x < 1024; x++) {
            want[y][x] = g_sys[g_screen[y][x] & 15];
        }
    }
    for (i = 0; i < 3; i++) {
        testpat_set_bpp(depths[i]);
        g_ci = CI_136;
        active();
        g_ci = CI_210;
        CHECK_EQ(g_s.console_depth, depths[i]);
        CHECK_EQ(g_s.depth, 8);
        CHECK(g_s.reduce != NULL);
        CHECK(g_s.upd_palette);
        update_pump(&g_s);
        take_fixed_palette(PALETTE);
        clear_screen();
        take_bitmaps();
        drain();
        CHECK_EQ(seen_count(), 1024UL * 768UL);
        CHECK_MEM(g_screen, want, sizeof(want));
        CHECK_EQ(g_s.ending, END_NO);

        core_frame_changed();
        rdp_pump(&g_s);
        clear_screen();
        take_bitmaps();
        drain();
        CHECK_EQ(seen_count(), 1024UL * 768UL);
        CHECK_MEM(g_screen, want, sizeof(want));
    }
    testpat_set_bpp(8);
    reset();
}

static void finalize_2(void)
{
    feed(CONFIRM_2);
    CHECK_EQ(g_s.state, ST_FINALIZE);
    feed(C_FOUR_2);
    expect_all(__LINE__, SYNC_2, COOP_2, GRANTED_2, FONTMAP_2, NULL);
    CHECK_EQ(g_s.state, ST_ACTIVE);
    pointer_pump(&g_s);
    TAKE(PTR_DEFAULT_2);
}

static void test_reactivate(void)
{
    static unsigned char want[768][1024];
    unsigned long crc, events;
    unsigned int x, y, at;

    active();
    update_pump(&g_s);
    TAKE(PALETTE);
    drain();
    testpat_set_mode(800, 600, 8);
    EXPECT("");
    update_area(&g_s, 0, 0, 1024, 768);
    update_pump(&g_s);
    EXPECT("");
    CHECK(g_s.mode_due);
    g_s.phase_ms = GetTickCount() - 60000UL;
    rdp_pump(&g_s);
    EXPECT2(DEACTIVATE_1, DEMAND_800_2);
    CHECK_EQ(g_s.state, ST_WAIT_CONFIRM);
    CHECK(GetTickCount() - g_s.phase_ms < 5000UL);
    CHECK_EQ(g_s.share_id, 0x000203EAUL);
    CHECK_EQ(g_s.width, 800);
    CHECK_EQ(g_s.height, 600);
    CHECK_EQ(g_s.depth, 8);
    CHECK(!g_s.mode_due);
    events = g_s.in_events;
    feed(C_INSYNC);
    CHECK_EQ(g_s.in_events, events + 1);
    CHECK(!update_area(&g_s, 0, 0, 800, 600));
    rdp_pump(&g_s);
    EXPECT("");
    finalize_2();
    update_pump(&g_s);
    TAKE(PALETTE_2);
    clear_screen();
    CHECK_EQ(take_bitmaps() + drain(), SCREEN_800_PDUS);
    CHECK_EQ(g_crc ^ 0xFFFFFFFFUL, SCREEN_800_CRC);
    CHECK_EQ(seen_count(), 800UL * 600UL);
    crc = 0xFFFFFFFFUL;
    for (y = 0; y < 600; y++) {
        crc = crc32_add(crc, g_screen[y], 800);
    }
    CHECK_EQ(crc ^ 0xFFFFFFFFUL, PICTURE_800_CRC);
    CHECK_EQ(g_s.ending, END_NO);

    testpat_set_mode(1024, 768, 8);
    active();
    update_pump(&g_s);
    TAKE(PALETTE);
    drain();
    testpat_set_mode(800, 600, 8);
    rdp_pump(&g_s);
    EXPECT2(DEACTIVATE_1, DEMAND_800_2);
    testpat_set_mode(1024, 768, 8);
    rdp_pump(&g_s);
    EXPECT("");
    CHECK(g_s.mode_due);
    feed(CONFIRM_2);
    rdp_pump(&g_s);
    EXPECT("");
    feed(C_FOUR_2);
    expect_all(__LINE__, SYNC_2, COOP_2, GRANTED_2, FONTMAP_2, NULL);
    CHECK_EQ(g_s.state, ST_ACTIVE);
    rdp_pump(&g_s);
    EXPECT2(DEACTIVATE_2, DEMAND_1024_3);
    CHECK_EQ(g_s.state, ST_WAIT_CONFIRM);
    CHECK_EQ(g_s.share_id, 0x000303EAUL);
    CHECK_EQ(g_s.width, 1024);
    CHECK_EQ(g_s.height, 768);

    active();
    update_pump(&g_s);
    TAKE(PALETTE);
    drain();
    testpat_set_mode(800, 600, 8);
    update_area(&g_s, 0, 0, 64, 64);
    update_pump(&g_s);
    CHECK(g_s.mode_due);
    testpat_set_mode(1024, 768, 8);
    rdp_pump(&g_s);
    TAKE(PALETTE);
    clear_screen();
    CHECK_EQ(take_bitmaps() + drain(), SCREEN_PDUS);
    CHECK_EQ(g_crc ^ 0xFFFFFFFFUL, SCREEN_CRC);
    CHECK_EQ(g_s.state, ST_ACTIVE);
    CHECK_EQ(g_s.share_id, 0x000103EAUL);
    CHECK(!g_s.mode_due);

    joined();
    testpat_set_mode(800, 600, 8);
    rdp_pump(&g_s);
    EXPECT("");
    feed(INFO_MSTSC);
    EXPECT2(LICENSE, DEMAND_800_1);
    CHECK_EQ(g_s.state, ST_WAIT_CONFIRM);

    testpat_set_mode(1024, 768, 8);
    g_ci = CI_136;
    active();
    g_ci = CI_210;
    update_pump(&g_s);
    TAKE(PALETTE);
    clear_screen();
    take_bitmaps();
    drain();
    CHECK_EQ(picture_crc(), PICTURE_CRC);
    for (y = 0; y < 768; y++) {
        for (x = 0; x < 1024; x++) {
            want[y][x] = g_sys[g_screen[y][x] & 15];
        }
    }
    testpat_set_mode(1024, 768, 16);
    rdp_pump(&g_s);
    EXPECT2(DEACTIVATE_1, DEMAND_1024_2);
    CHECK_EQ(g_s.console_depth, 16);
    CHECK_EQ(g_s.depth, 8);
    finalize_2();
    CHECK(g_s.reduce != NULL);
    update_pump(&g_s);
    take_fixed_palette(PALETTE_2);
    clear_screen();
    take_bitmaps();
    drain();
    CHECK_EQ(seen_count(), 1024UL * 768UL);
    CHECK_MEM(g_screen, want, sizeof(want));
    testpat_set_mode(1024, 768, 8);
    rdp_pump(&g_s);
    EXPECT2(DEACTIVATE_2, DEMAND_1024_3);
    CHECK_EQ(g_s.console_depth, 8);
    feed(CONFIRM_2);
    feed(C_FOUR_2);
    CHECK_EQ(g_s.state, ST_ACTIVE);
    CHECK(g_s.reduce == NULL);
    CHECK(g_s.upd_palette);
    g_s.tx_len = 0;

    active();
    update_pump(&g_s);
    TAKE(PALETTE);
    drain();
    testpat_set_mode(1024, 768, 24);
    rdp_pump(&g_s);
    EXPECT2(DEACTIVATE_1, DEMAND_24_2);
    CHECK_EQ(g_s.console_depth, 24);
    CHECK_EQ(g_s.depth, 24);
    finalize_2();
    CHECK(g_s.reduce == NULL);
    update_pump(&g_s);
    at = (g_s.tx[13] & 0x80) ? 15 : 14;
    CHECK(g_s.tx_len > at + 22 + 18);
    CHECK_EQ(g_s.tx[at + 14], 2);
    CHECK_EQ(get16(g_s.tx + at + 18), 1);
    CHECK_EQ(get16(g_s.tx + at + 22 + 12), 24);
    g_s.tx_len = 0;
    CHECK_EQ(g_s.ending, END_NO);

    testpat_set_mode(1024, 768, 8);
    reset();
}

static void test_update_row(void)
{
    static const unsigned char p8[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    static const unsigned char p16[8] = {
        0x01, 0x80, 0x02, 0x00, 0x03, 0xFF, 0x04, 0x7F
    };
    static const unsigned char p24[6] = { 1, 2, 3, 4, 5, 6 };
    static const unsigned char p32[8] = { 1, 2, 3, 0xFF, 4, 5, 6, 0xEE };
    static const unsigned char p565[8] = {
        0x00, 0x00, 0x18, 0xC6, 0xFF, 0xFF, 0x1F, 0x00
    };
    static unsigned char table[REDUCE_TABLE_SIZE];
    unsigned char d[16];
    CORE_FRAME f;

    memset(&f, 0, sizeof(f));
    f.width = 4;
    f.height = 2;
    f.bpp = 8;
    f.bits = p8;
    f.stride = 4;
    CHECK_EQ(update_row(d, &f, NULL, 0, 1, 4), 4);
    CHECK_MEM(d, "\x05\x06\x07\x08", 4);
    memset(d, 0xAA, sizeof(d));
    CHECK_EQ(update_row(d, &f, NULL, 2, 0, 8), 8);
    CHECK_MEM(d, "\x03\x04\0\0\0\0\0\0", 8);
    memset(d, 0xAA, sizeof(d));
    CHECK_EQ(update_row(d, &f, NULL, 0, 2, 4), 4);
    CHECK_MEM(d, "\0\0\0\0", 4);
    memset(d, 0xAA, sizeof(d));
    CHECK_EQ(update_row(d, &f, NULL, 4, 0, 4), 4);
    CHECK_MEM(d, "\0\0\0\0", 4);
    f.bits = p8 + 4;
    f.stride = -4;
    update_row(d, &f, NULL, 0, 0, 4);
    CHECK_MEM(d, "\x05\x06\x07\x08", 4);
    update_row(d, &f, NULL, 1, 1, 4);
    CHECK_MEM(d, "\x02\x03\x04\0", 4);

    f.width = 4;
    f.height = 1;
    f.bpp = 16;
    f.green_mask = 0x07E0;
    f.bits = p16;
    f.stride = 8;
    CHECK_EQ(update_row(d, &f, NULL, 0, 0, 4), 8);
    CHECK_MEM(d, p16, 8);
    f.green_mask = 0x03E0;
    memset(d, 0xAA, sizeof(d));
    CHECK_EQ(update_row(d, &f, NULL, 1, 0, 4), 8);
    CHECK_MEM(d, "\x02\x00\x03\x7F\x04\x7F\0\0", 8);

    f.width = 2;
    f.bpp = 24;
    f.green_mask = 0;
    f.bits = p24;
    f.stride = 6;
    memset(d, 0xAA, sizeof(d));
    CHECK_EQ(update_row(d, &f, NULL, 1, 0, 4), 12);
    CHECK_MEM(d, "\x04\x05\x06\0\0\0\0\0\0\0\0\0", 12);

    f.bpp = 32;
    f.bits = p32;
    f.stride = 8;
    memset(d, 0xAA, sizeof(d));
    CHECK_EQ(update_row(d, &f, NULL, 0, 0, 4), 12);
    CHECK_MEM(d, "\x01\x02\x03\x04\x05\x06\0\0\0\0\0\0", 12);

    f.width = 4;
    f.bpp = 16;
    f.red_mask = 0xF800;
    f.green_mask = 0x07E0;
    f.blue_mask = 0x001F;
    f.bits = p565;
    reduce_table(table, &f);
    memset(d, 0xAA, sizeof(d));
    CHECK_EQ(update_row(d, &f, table, 0, 0, 4), 4);
    CHECK_MEM(d, "\x00\x07\xFF\xFC", 4);
    memset(d, 0xAA, sizeof(d));
    CHECK_EQ(update_row(d, &f, table, 2, 0, 4), 4);
    CHECK_MEM(d, "\xFF\xFC\0\0", 4);
}

static int cell_is(const UPDATE_CELL *c, unsigned int x0, unsigned int y0,
                   unsigned int x1, unsigned int y1)
{
    return c->x0 == x0 && c->y0 == y0 && c->x1 == x1 && c->y1 == y1;
}

static void test_map(void)
{
    static const struct {
        int depth;
        unsigned int cell_h, rows, palette;
    } g[4] = {
        { 24, 42, 19, 0 }, { 16, 63, 13, 0 }, { 15, 63, 13, 0 },
        { 8, 64, 12, 1 }
    };
    static SESSION m;
    int i;

    for (i = 0; i < 4; i++) {
        memset(&m, 0, sizeof(m));
        m.width = 1024;
        m.height = 768;
        m.depth = g[i].depth;
        CHECK_EQ(update_area(&m, 0, 0, 10, 10), 0);
        update_screen(&m);
        CHECK_EQ(m.map.cols, 16);
        CHECK_EQ(m.map.cell_h, g[i].cell_h);
        CHECK_EQ(m.map.rows, g[i].rows);
        CHECK_EQ(m.map.dirty, 16 * g[i].rows);
        CHECK_EQ(m.upd_palette, g[i].palette);
        CHECK(cell_is(&m.map.cell[0], 0, 0, 64, g[i].cell_h));
        CHECK(cell_is(&m.map.cell[16 * g[i].rows - 1], 0, 0, 64,
                      768 - (g[i].rows - 1) * g[i].cell_h));
        CHECK_EQ(m.ending, END_NO);
    }

    m.width = 1030;
    update_screen(&m);
    CHECK_EQ(m.map.cols, 17);
    CHECK(cell_is(&m.map.cell[16], 0, 0, 8, 64));
    m.width = 4096;
    m.height = 4096;
    m.depth = 24;
    update_screen(&m);
    CHECK(m.map.cols == 64 && m.map.rows == 98);
    CHECK_EQ(m.ending, END_NO);
    m.width = 8192;
    update_screen(&m);
    CHECK_EQ(m.map.cols, 0);
    CHECK_EQ(m.ending, END_NOW);
    CHECK_EQ(update_area(&m, 0, 0, 10, 10), 0);

    memset(&m, 0, sizeof(m));
    m.width = 1024;
    m.height = 768;
    m.depth = 8;
    update_screen(&m);
    memset(m.map.cell, 0, sizeof(m.map.cell));
    m.map.dirty = 0;
    CHECK_EQ(update_area(&m, 70, 10, 80, 20), 1);
    CHECK(cell_is(&m.map.cell[1], 4, 10, 16, 20));
    CHECK_EQ(update_area(&m, 101, 50, 103, 52), 1);
    CHECK(cell_is(&m.map.cell[1], 4, 10, 40, 52));
    CHECK_EQ(m.map.dirty, 1);
    CHECK_EQ(update_area(&m, 60, 60, 70, 70), 1);
    CHECK_EQ(m.map.dirty, 4);
    CHECK(cell_is(&m.map.cell[0], 60, 60, 64, 64));
    CHECK(cell_is(&m.map.cell[1], 0, 10, 40, 64));
    CHECK(cell_is(&m.map.cell[16], 60, 0, 64, 6));
    CHECK(cell_is(&m.map.cell[17], 0, 0, 8, 6));
    CHECK_EQ(update_area(&m, 2000, 0, 2010, 10), 0);
    CHECK_EQ(update_area(&m, 10, 10, 5, 20), 0);
    CHECK_EQ(update_area(&m, -100, -100, 0, 0), 0);
    CHECK_EQ(m.map.dirty, 4);
    CHECK_EQ(update_area(&m, 1021, 766, 5000, 5000), 1);
    CHECK_EQ(m.map.dirty, 5);
    CHECK(cell_is(&m.map.cell[11 * 16 + 15], 60, 62, 64, 64));
}

static void put_order(int kind, long l, long t, long r, long b,
                      unsigned long color, unsigned int rop, int sx, int sy);

static void test_damage_list(void)
{
    RECT r[3];
    unsigned long at;
    int i;

    active();
    update_pump(&g_s);
    TAKE(PALETTE);
    drain();
    set_rect(&r[0], 0, 0, 32, 32);
    set_rect(&r[1], 32, 0, 64, 10);
    set_rect(&r[2], 600, 700, 632, 732);
    core_damage(r, 3);
    CHECK_EQ(g_s.ev_nrects, 2);
    CHECK(g_s.ev_rects[0].right == 64 && g_s.ev_rects[0].bottom == 32);
    at = g_s.ev_at[0];
    CHECK_EQ(g_s.ev_at[1], at);
    for (i = 0; i < SESS_EV_RECTS; i++) {
        if (i == 62) {
            put_order(CORE_ORDER_FILL, 900, 10, 904, 12, 7, 0xF0, 0, 0);
        }
        set_rect(&r[0], 0, 100 + 2 * i, 4, 101 + 2 * i);
        core_damage(r, 1);
    }
    CHECK_EQ(g_s.ev_nrects, SESS_EV_RECTS);
    CHECK_EQ(g_s.ev_rects[SESS_EV_RECTS - 1].top, 100 + 2 * 61);
    CHECK_EQ(g_s.ev_rects[SESS_EV_RECTS - 1].bottom, 101 + 2 * 63);
    CHECK_EQ(g_s.ev_at[SESS_EV_RECTS - 1], at);
    rdp_pump(&g_s);
    CHECK_EQ(g_s.ev_nrects, 0);
    clear_screen();
    drain();
    CHECK_EQ(g_seen[0][0] + g_seen[31][63] + g_seen[700][600], 3);
    CHECK_EQ(g_seen[100][0] + g_seen[226][3], 2);
    CHECK_EQ(g_seen[32][0] + g_seen[64][0] + g_seen[699][600], 0);
    CHECK_EQ(g_s.ending, END_NO);
}

static unsigned char g_ptr[4096];

static void test_cursor(CORE_POINTER *p, unsigned int hx)
{
    int x, y;

    memset(p, 0, sizeof(*p));
    p->cb = sizeof(*p);
    p->width = 32;
    p->height = 32;
    p->hot_x = hx;
    memset(p->and_mask, 0xFF, sizeof(p->and_mask));
    for (y = 0; y < 10; y++) {
        for (x = 0; x <= y; x++) {
            p->and_mask[y][x >> 3] &= (unsigned char) ~(0x80 >> (x & 7));
            if (x != 0 && x != y && y != 9) {
                memset(&p->color[y][x * 3], 0xFF, 3);
            }
        }
    }
    for (x = 0; x < 4; x++) {
        memset(&p->color[12][x * 3], 0xFF, 3);
    }
}

static void shape(const CORE_POINTER *p)
{
    core_pointer_shape(p);
    rdp_pump(&g_s);
}

static unsigned int take_pointer(void)
{
    const unsigned char *p = g_s.tx;
    unsigned int len, at, n;

    len = g_s.tx_len >= 4 ? (p[2] << 8) | p[3] : 0;
    at = g_s.tx_len >= 14 && (p[13] & 0x80) ? 15 : 14;
    if (g_s.tx_len < 14 || len < at + 22 || len > g_s.tx_len ||
        len - at - 18 > sizeof(g_ptr)) {
        t_failures++;
        printf("t_proto.c: take_pointer: no Pointer PDU (%u bytes wait)\n",
               g_s.tx_len);
        g_s.tx_len = 0;
        return 0;
    }
    CHECK_EQ(get16(p + at), len - at);
    CHECK_EQ(get16(p + at + 6), 0x03EA);
    CHECK_EQ(get16(p + at + 8), 0x0001);
    CHECK_EQ(p[at + 14], 27);
    n = len - at - 18;
    memcpy(g_ptr, p + at + 18, n);
    memmove(g_s.tx, g_s.tx + len, g_s.tx_len - len);
    g_s.tx_len -= len;
    return n;
}

static void test_pointer(void)
{
    CORE_POINTER p;
    unsigned int i;

    active();
    CHECK(g_s.caps.pointer_new);
    CHECK_EQ(g_s.ptr.slots, 20);
    update_pump(&g_s);
    TAKE(PALETTE);
    drain();

    for (i = 0; i < 3; i++) {
        test_cursor(&p, i + 1);
        shape(&p);
        CHECK_EQ(take_pointer(), 276);
        CHECK_EQ(get16(g_ptr), 8);
        CHECK_EQ(get16(g_ptr + 4), 1);
        CHECK_EQ(get16(g_ptr + 6), i);
        CHECK_EQ(get16(g_ptr + 8), i + 1);
    }
    test_cursor(&p, 0);
    shape(&p);
    CHECK_EQ(crc32_add(0xFFFFFFFFUL, g_s.tx + 33, g_s.tx_len - 33) ^
             0xFFFFFFFFUL, 0x07E8C800UL);
    EXPECT(PTR_NEW1_3);
    shape(&p);
    EXPECT("");
    test_cursor(&p, 2);
    shape(&p);
    CHECK_EQ(take_pointer(), 6);
    CHECK_EQ(get16(g_ptr), 7);
    CHECK_EQ(get16(g_ptr + 4), 1);
    test_cursor(&p, 0);
    shape(&p);
    EXPECT(PTR_CACHED_3);

    memset(&p, 0, sizeof(p));
    p.cb = sizeof(p);
    shape(&p);
    EXPECT(PTR_HIDDEN);
    shape(&p);
    EXPECT("");
    core_pointer_pos(320, 240);
    rdp_pump(&g_s);
    EXPECT("");
    test_cursor(&p, 0);
    shape(&p);
    EXPECT(PTR_CACHED_3);
    shape(NULL);
    EXPECT(PTR_DEFAULT);

    core_pointer_pos(320, 240);
    rdp_pump(&g_s);
    EXPECT(PTR_POSITION);
    feed(IN_MOVE_CLICK);
    core_pointer_pos(400, 300);
    rdp_pump(&g_s);
    EXPECT("");
    core_pointer_pos(401, 300);
    rdp_pump(&g_s);
    EXPECT("");
    g_s.mouse_ms -= 2 * POINTER_ECHO_MS;
    core_pointer_pos(500, 10);
    core_pointer_pos(320, 240);
    rdp_pump(&g_s);
    EXPECT(PTR_POSITION);
    g_s.suppressed = 1;
    core_pointer_pos(330, 240);
    rdp_pump(&g_s);
    EXPECT("");
    CHECK(g_s.ptr.pos_due);
    core_pointer_pos(400, 300);
    rdp_pump(&g_s);
    CHECK(!g_s.ptr.pos_due);
    shape(&p);
    EXPECT("");
    g_s.suppressed = 0;
    rdp_pump(&g_s);
    EXPECT(PTR_CACHED_3);

    for (i = 4; i < 20; i++) {
        test_cursor(&p, i);
        shape(&p);
        CHECK_EQ(take_pointer(), 276);
        CHECK_EQ(get16(g_ptr + 6), i);
    }
    test_cursor(&p, 20);
    shape(&p);
    CHECK_EQ(take_pointer(), 276);
    CHECK_EQ(get16(g_ptr + 6), 0);
    test_cursor(&p, 1);
    shape(&p);
    CHECK_EQ(take_pointer(), 276);
    CHECK_EQ(get16(g_ptr + 6), 2);
    test_cursor(&p, 2);
    shape(&p);
    CHECK_EQ(take_pointer(), 6);
    CHECK_EQ(get16(g_ptr + 4), 1);
    test_cursor(&p, 0);
    p.color[0][0] = 0x12;
    p.color[0][1] = 0x34;
    p.color[0][2] = 0x56;
    shape(&p);
    CHECK_EQ(take_pointer(), 3218);
    CHECK_EQ(get16(g_ptr), 6);
    CHECK_EQ(get16(g_ptr + 4), 3);
    CHECK_EQ(get16(g_ptr + 10), 32);
    CHECK_EQ(get16(g_ptr + 14), 128);
    CHECK_EQ(get16(g_ptr + 16), 3072);
    CHECK_EQ(g_ptr[18 + 31 * 96], 0x56);
    CHECK_EQ(g_ptr[18 + 31 * 96 + 1], 0x34);
    CHECK_EQ(g_ptr[18 + 31 * 96 + 2], 0x12);
    CHECK_EQ(g_ptr[18 + 3072 + 31 * 4], 0x7F);
    CHECK_EQ(g_ptr[18 + 3072], 0xFF);
    CHECK_EQ(g_s.ending, END_NO);

    licensed();
    feed(CONFIRM_RDP4);
    feed(C_FOUR);
    expect_all(__LINE__, SYNC, COOP, GRANTED, FONTMAP, NULL);
    CHECK(!g_s.caps.pointer_new);
    CHECK_EQ(g_s.ptr.slots, 20);
    pointer_pump(&g_s);
    TAKE(PTR_DEFAULT);
    update_pump(&g_s);
    TAKE(PALETTE);
    drain();
    for (i = 0; i < 3; i++) {
        test_cursor(&p, i + 1);
        shape(&p);
        CHECK_EQ(take_pointer(), 3218);
        CHECK_EQ(get16(g_ptr), 6);
        CHECK_EQ(get16(g_ptr + 4), i);
    }
    test_cursor(&p, 0);
    shape(&p);
    CHECK_EQ(g_s.tx_len, PTR_COLOR_3_LEN);
    CHECK_EQ(crc32_add(0xFFFFFFFFUL, g_s.tx, g_s.tx_len) ^ 0xFFFFFFFFUL,
             PTR_COLOR_3_CRC);
    CHECK_EQ(take_pointer(), 3218);
    CHECK_EQ(crc32_add(0xFFFFFFFFUL, g_ptr, 3218) ^ 0xFFFFFFFFUL,
             0xA4522C19UL);
    g_s.caps.color_pointer_cache = 0;
    pointer_reset(&g_s);
    CHECK_EQ(g_s.ptr.slots, 1);
    rdp_pump(&g_s);
    CHECK_EQ(take_pointer(), 3218);
    CHECK_EQ(get16(g_ptr + 4), 0);
    test_cursor(&p, 1);
    shape(&p);
    CHECK_EQ(take_pointer(), 3218);
    CHECK_EQ(get16(g_ptr + 4), 0);
    test_cursor(&p, 0);
    shape(&p);
    CHECK_EQ(take_pointer(), 3218);
    CHECK_EQ(get16(g_ptr), 6);
    CHECK_EQ(g_s.tx_len, 0);
    CHECK_EQ(g_s.ending, END_NO);
}

static void decode(INPUT_EVENT *e, unsigned int type, unsigned int flags,
                   unsigned int a, unsigned int b)
{
    unsigned char ev[INPUT_EVENT_SIZE];

    memset(ev, 0, sizeof(ev));
    ev[4] = (unsigned char) type;
    ev[5] = (unsigned char) (type >> 8);
    ev[6] = (unsigned char) flags;
    ev[7] = (unsigned char) (flags >> 8);
    ev[8] = (unsigned char) a;
    ev[9] = (unsigned char) (a >> 8);
    ev[10] = (unsigned char) b;
    ev[11] = (unsigned char) (b >> 8);
    input_decode(ev, 1024, 768, e);
}

#define KEY(f, c)        decode(&e, 0x0004, f, c, 0)
#define MOUSE(f, x, y)   decode(&e, 0x8001, f, x, y)

static void test_decode(void)
{
    INPUT_EVENT e;

    KEY(0x0000, 0x1E);
    CHECK_EQ(e.kind, IN_KEY);
    CHECK_EQ(e.code, 0x1E);
    CHECK_EQ(e.ext, 0);
    CHECK_EQ(e.up, 0);
    KEY(0x4000, 0x1E);
    CHECK_EQ(e.kind, IN_KEY);
    CHECK_EQ(e.up, 0);
    KEY(0xC000, 0x1E);
    CHECK_EQ(e.up, 1);
    KEY(0x8000, 0x1E);
    CHECK_EQ(e.up, 1);
    KEY(0x0100, 0x48);
    CHECK_EQ(e.ext, 0xE0);
    KEY(0xC100, 0x48);
    CHECK_EQ(e.ext, 0xE0);
    CHECK_EQ(e.up, 1);
    KEY(0x8200, 0x1D);
    CHECK_EQ(e.ext, 0xE1);
    CHECK_EQ(e.up, 1);
    KEY(0x0000, 0x7F);
    CHECK_EQ(e.kind, IN_KEY);
    KEY(0x0000, 0x80);
    CHECK_EQ(e.kind, IN_SKIP);

    decode(&e, 0x0000, 0, 0x0007, 0x0001);
    CHECK_EQ(e.kind, IN_SYNC);
    CHECK_EQ(e.code, 0x00010007UL);

    MOUSE(0x0800, 400, 300);
    CHECK_EQ(e.kind, IN_MOVE);
    CHECK_EQ(e.x, 400);
    CHECK_EQ(e.y, 300);
    MOUSE(0x9800, 10, 20);
    CHECK_EQ(e.kind, IN_MOVE);
    MOUSE(0x9000, 10, 20);
    CHECK_EQ(e.kind, IN_BUTTON);
    CHECK_EQ(e.code, 1);
    CHECK_EQ(e.up, 0);
    CHECK_EQ(e.x, 10);
    CHECK_EQ(e.y, 20);
    MOUSE(0x1000, 10, 20);
    CHECK_EQ(e.code, 1);
    CHECK_EQ(e.up, 1);
    MOUSE(0xA000, 10, 20);
    CHECK_EQ(e.code, 2);
    CHECK_EQ(e.up, 0);
    MOUSE(0x2000, 10, 20);
    CHECK_EQ(e.code, 2);
    CHECK_EQ(e.up, 1);
    MOUSE(0xC000, 10, 20);
    CHECK_EQ(e.kind, IN_BUTTON);
    CHECK_EQ(e.code, 3);
    CHECK_EQ(e.up, 0);
    MOUSE(0x4000, 10, 20);
    CHECK_EQ(e.code, 3);
    CHECK_EQ(e.up, 1);
    MOUSE(0x9400, 10, 20);
    CHECK_EQ(e.kind, IN_BUTTON);
    CHECK_EQ(e.code, 1);
    MOUSE(0x0000, 10, 20);
    CHECK_EQ(e.kind, IN_SKIP);
    MOUSE(0x8000, 10, 20);
    CHECK_EQ(e.kind, IN_SKIP);
    MOUSE(0x3000, 10, 20);
    CHECK_EQ(e.kind, IN_SKIP);
    MOUSE(0x0278, 400, 300);
    CHECK_EQ(e.kind, IN_WHEEL);
    CHECK_EQ(e.wheel, 120);
    MOUSE(0x0388, 400, 300);
    CHECK_EQ(e.wheel, (unsigned long) -120);
    MOUSE(0x8280, 400, 300);
    CHECK_EQ(e.kind, IN_WHEEL);
    CHECK_EQ(e.wheel, 128);
    MOUSE(0x8380, 400, 300);
    CHECK_EQ(e.wheel, (unsigned long) -128);
    MOUSE(0x02FF, 0, 0);
    CHECK_EQ(e.wheel, 255);
    MOUSE(0x0300, 0, 0);
    CHECK_EQ(e.wheel, (unsigned long) -256);
    MOUSE(0x1278, 400, 300);
    CHECK_EQ(e.kind, IN_SKIP);

    MOUSE(0x0800, 0xFFFB, 2000);
    CHECK_EQ(e.x, 0);
    CHECK_EQ(e.y, 767);
    MOUSE(0x0800, 1024, 768);
    CHECK_EQ(e.x, 1023);
    CHECK_EQ(e.y, 767);
    MOUSE(0x0800, 0x7FFF, 0x8000);
    CHECK_EQ(e.x, 1023);
    CHECK_EQ(e.y, 0);

    decode(&e, 0x8002, 0x8001, 400, 300);
    CHECK_EQ(e.kind, IN_SKIP);
    CHECK_EQ(e.type, 0x8002);
    decode(&e, 0x0002, 0, 0x41, 0);
    CHECK_EQ(e.kind, IN_SKIP);
    decode(&e, 0x0005, 0, 0x41, 0);
    CHECK_EQ(e.kind, IN_SKIP);
}

static void test_client_pdus(void)
{
    licensed();
    feed(CONFIRM_RDP4);
    feed(C_INSYNC);
    EXPECT("");
    CHECK_EQ(g_s.in_events, 1);

    active();
    update_pump(&g_s);
    TAKE(PALETTE);
    drain();
    feed(IN_A_DOWN);
    CHECK_EQ(g_s.in_events, 1);
    feed(IN_SYNC_UP);
    CHECK_EQ(g_s.in_events, 4);
    feed(IN_PAUSE);
    CHECK_EQ(g_s.in_events, 8);
    feed(IN_MOVE_CLICK);
    feed(IN_WHEEL_UP);
    feed(IN_WHEEL_DOWN);
    feed(IN_WHEEL_RD);
    CHECK_EQ(g_s.in_events, 13);
    feed(IN_MOUSEX);
    CHECK_EQ(g_s.in_events, 14);
    CHECK_EQ(g_s.ev_logged_n, 1);
    feed(IN_SHORT);
    CHECK_EQ(g_s.in_events, 16);
    feed(IN_EXTRA);
    CHECK_EQ(g_s.in_events, 17);
    feed(IN_TINY);
    CHECK_EQ(g_s.in_events, 17);
    feed(IN_ODD);
    CHECK_EQ(g_s.in_events, 25);
    CHECK_EQ(g_s.ev_logged_n, 5);
    update_pump(&g_s);
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NO);
    CHECK_EQ(g_s.state, ST_ACTIVE);

    feed(C_SUPPRESS);
    CHECK(g_s.suppressed);
    feed(C_REFRESH_SMALL);
    update_pump(&g_s);
    EXPECT("");
    CHECK_EQ(g_s.map.dirty, 0);
    feed(C_SUPPRESS_EMPTY);
    CHECK(g_s.suppressed);
    feed(C_ALLOW);
    CHECK(!g_s.suppressed);
    update_pump(&g_s);
    TAKE(PALETTE);
    clear_screen();
    CHECK_EQ(take_bitmaps() + drain(), SCREEN_PDUS);
    CHECK_EQ(seen_count(), 1024UL * 768UL);
    CHECK_EQ(picture_crc(), PICTURE_CRC);
    feed(C_REFRESH_BIG);
    feed(C_SUPPRESS);
    update_pump(&g_s);
    EXPECT("");
    feed(C_ALLOW_200);
    CHECK(!g_s.suppressed);
    update_pump(&g_s);
    TAKE(PALETTE);
    clear_screen();
    drain();
    CHECK_EQ(seen_count(), 1024UL * 768UL);
    CHECK_EQ(picture_crc(), PICTURE_CRC);
    CHECK_EQ(g_s.ending, END_NO);

    licensed();
    feed(CONFIRM_MSTSC);
    feed(C_SUPPRESS);
    feed(C_FOUR);
    expect_all(__LINE__, SYNC, COOP, GRANTED, FONTMAP, NULL);
    update_pump(&g_s);
    EXPECT("");
    feed(C_ALLOW);
    update_pump(&g_s);
    TAKE(PALETTE);
    clear_screen();
    drain();
    CHECK_EQ(picture_crc(), PICTURE_CRC);
}

extern int          t_input_calls;
extern unsigned int t_input_locks;
extern PROMPT_DONE  t_prompt_done;
extern void        *t_prompt_ctx;
extern int          t_prompt_open;
extern char         t_prompt_address[64];
extern unsigned int t_prompt_timeout;
extern char         t_input_desktop[64];

#define DENIED_7 \
    "03 00 00 24 02 F0 80 68 00 01 03 EB 70 16 16 00 17 00 00 00 00 00" \
    "00 00 00 01 16 00 2F 00 00 00 07 00 00 00"

static void answer(int a)
{
    PROMPT_DONE done = t_prompt_done;

    CHECK(done != NULL);
    if (done != NULL) {
        done(t_prompt_ctx, a);
    }
}

static void active_held(void)
{
    licensed();
    CHECK_EQ(t_prompt_open, 1);
    feed(CONFIRM_MSTSC);
    feed(C_FOUR);
    expect_all(__LINE__, SYNC, COOP, GRANTED, FONTMAP, NULL);
    CHECK_EQ(g_t->state, ST_ACTIVE);
    rdp_pump(g_t);
    EXPECT("");
    CHECK(g_t->held);
}

static void test_prompt(void)
{
    int calls;

    core_set_prompt(30);

    active_held();
    CHECK_EQ(t_prompt_timeout, 30);
    CHECK(strcmp(t_prompt_address, "test") == 0);
    calls = t_input_calls;
    feed(IN_A_DOWN);
    feed(IN_SYNC_UP);
    CHECK_EQ(t_input_calls, calls);
    rdp_pump(&g_s);
    EXPECT("");
    answer(PROMPT_YES);
    CHECK_EQ(t_input_calls, calls + 1);
    CHECK_EQ(t_input_locks, 2);
    rdp_pump(&g_s);
    CHECK(!g_s.held);
    TAKE(PTR_DEFAULT);
    TAKE(PALETTE);
    clear_screen();
    take_bitmaps();
    drain();
    CHECK_EQ(seen_count(), 1024UL * 768UL);
    CHECK_EQ(picture_crc(), PICTURE_CRC);
    feed(IN_A_DOWN);
    CHECK_EQ(t_input_calls, calls + 2);
    CHECK_EQ(g_s.ending, END_NO);

    active_held();
    answer(PROMPT_NO);
    rdp_pump(&g_s);
    EXPECT2(DENIED_7, DPUM_1);
    CHECK_EQ(g_s.ending, END_FLUSH);
    rdp_pump(&g_s);
    EXPECT("");

    licensed();
    g_s.early_flags = 0;
    answer(PROMPT_TIMEOUT);
    rdp_pump(&g_s);
    EXPECT(DPUM_1);
    CHECK_EQ(g_s.ending, END_FLUSH);

    connected();
    feed(CI_210);
    EXPECT(CRESP_0);
    CHECK_EQ(t_prompt_open, 0);

    licensed();
    answer(PROMPT_FAILED);
    rdp_pump(&g_s);
    EXPECT2(DENIED_7, DPUM_1);
    CHECK_EQ(g_s.ending, END_FLUSH);

    licensed();
    CHECK_EQ(t_prompt_open, 1);
    rdp_done(&g_s);
    CHECK_EQ(t_prompt_open, 0);
    CHECK(t_prompt_done == NULL);
    memset(&g_s, 0, sizeof(g_s));
    g_open = 0;

    strcpy(t_input_desktop, "Winlogon");
    active();
    CHECK_EQ(t_prompt_open, 0);
    CHECK(!g_s.held);
    rdp_pump(&g_s);
    TAKE(PALETTE);
    strcpy(t_input_desktop, "Default");

    t_input_desktop[0] = '\0';
    licensed();
    CHECK_EQ(t_prompt_open, 1);
    strcpy(t_input_desktop, "Default");

    core_set_prompt(0);
    active();
    CHECK_EQ(t_prompt_open, 0);
    CHECK(!g_s.held);
}

extern int t_input_releases;

#define OTHER_5 \
    "03 00 00 24 02 F0 80 68 00 01 03 EB 70 16 16 00 17 00 00 00 00 00" \
    "00 00 00 01 16 00 2F 00 00 00 05 00 00 00"
#define RPC_1 \
    "03 00 00 24 02 F0 80 68 00 01 03 EB 70 16 16 00 17 00 00 00 00 00" \
    "00 00 00 01 16 00 2F 00 00 00 01 00 00 00"

static void discard(SESSION *s)
{
    int rounds;

    for (rounds = 0; rounds < 1000; rounds++) {
        s->tx_len = 0;
        rdp_pump(s);
        if (s->tx_len == 0) {
            break;
        }
    }
    s->tx_len = 0;
}

static void close_t(void)
{
    int *open = g_t == &g_s ? &g_open : &g_open2;

    if (*open) {
        rdp_done(g_t);
    }
    memset(g_t, 0, sizeof(*g_t));
    *open = 0;
}

static void second_info(void)
{
    g_t = &g_s2;
    joined();
    feed(INFO_MSTSC);
}

static void first_active(int prompt)
{
    g_t = &g_s;
    if (prompt) {
        active_held();
        answer(PROMPT_YES);
    } else {
        active();
    }
    discard(&g_s);
    CHECK(!g_s.held);
}

static void test_second(void)
{
    int calls, releases;

    first_active(0);
    g_t = &g_s2;
    connected();
    feed(CI_210);
    EXPECT(CRESP_0);
    close_t();
    g_t = &g_s;
    rdp_pump(&g_s);
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NO);

    releases = t_input_releases;
    second_info();
    EXPECT2(LICENSE, DEMAND_1024);
    CHECK_EQ(t_input_releases, releases + 1);
    g_t = &g_s;
    rdp_pump(&g_s);
    EXPECT2(OTHER_5, DPUM_1);
    CHECK_EQ(g_s.ending, END_FLUSH);
    CHECK(strstr(g_s.why, "a new client") != NULL);
    calls = t_input_calls;
    feed(IN_A_DOWN);
    CHECK_EQ(t_input_calls, calls);
    g_t = &g_s2;
    feed(CONFIRM_MSTSC);
    feed(C_FOUR);
    expect_all(__LINE__, SYNC, COOP, GRANTED, FONTMAP, NULL);
    CHECK_EQ(g_s2.state, ST_ACTIVE);
    feed(IN_A_DOWN);
    CHECK_EQ(t_input_calls, calls + 1);
    g_t = &g_s;
    close_t();
    CHECK_EQ(t_input_releases, releases + 1);
    g_t = &g_s2;
    close_t();
    CHECK_EQ(t_input_releases, releases + 2);

    core_set_new_client(1);
    first_active(0);
    second_info();
    expect_all(__LINE__, LICENSE, DENIED_7, DPUM_1, NULL);
    CHECK_EQ(g_s2.ending, END_FLUSH);
    CHECK(strstr(g_s2.why, "refused") != NULL);
    close_t();
    g_t = &g_s;
    rdp_pump(&g_s);
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NO);
    calls = t_input_calls;
    feed(IN_A_DOWN);
    CHECK_EQ(t_input_calls, calls + 1);
    close_t();
    second_info();
    EXPECT2(LICENSE, DEMAND_1024);
    close_t();
    core_set_new_client(0);

    core_set_prompt(30);
    first_active(1);
    second_info();
    EXPECT2(LICENSE, DEMAND_1024);
    g_t = &g_s;
    rdp_pump(&g_s);
    EXPECT("");
    calls = t_input_calls;
    feed(IN_A_DOWN);
    CHECK_EQ(t_input_calls, calls + 1);
    releases = t_input_releases;
    answer(PROMPT_YES);
    CHECK_EQ(t_input_releases, releases + 1);
    rdp_pump(&g_s);
    EXPECT2(OTHER_5, DPUM_1);
    g_t = &g_s2;
    feed(CONFIRM_MSTSC);
    feed(C_FOUR);
    expect_all(__LINE__, SYNC, COOP, GRANTED, FONTMAP, NULL);
    rdp_pump(&g_s2);
    CHECK(!g_s2.held);
    CHECK(g_s2.tx_len > 0);
    discard(&g_s2);
    close_t();
    g_t = &g_s;
    close_t();

    first_active(1);
    second_info();
    EXPECT2(LICENSE, DEMAND_1024);
    answer(PROMPT_NO);
    rdp_pump(&g_s2);
    EXPECT2(DENIED_7, DPUM_1);
    close_t();
    g_t = &g_s;
    rdp_pump(&g_s);
    EXPECT("");
    CHECK_EQ(g_s.ending, END_NO);
    calls = t_input_calls;
    feed(IN_A_DOWN);
    CHECK_EQ(t_input_calls, calls + 1);

    active_held();
    second_info();
    expect_all(__LINE__, LICENSE, DENIED_7, DPUM_1, NULL);
    CHECK_EQ(t_prompt_open, 1);
    close_t();
    g_t = &g_s;
    answer(PROMPT_YES);
    rdp_pump(&g_s);
    CHECK(!g_s.held);
    discard(&g_s);
    core_set_prompt(0);

    rdp_service_stop(&g_s);
    EXPECT2(RPC_1, DPUM_1);
    CHECK_EQ(g_s.ending, END_FLUSH);
    CHECK(strstr(g_s.why, "ntrdps stops") != NULL);
}

static void test_timeout(void)
{
    DWORD t0;

    g_t = &g_s;
    reset();
    t0 = g_s.phase_ms;
    CHECK(GetTickCount() - t0 < 5000UL);
    rdp_check_time(&g_s, t0 + RDP_HANDSHAKE_MS);
    CHECK_EQ(g_s.ending, END_NO);
    rdp_check_time(&g_s, t0 + RDP_HANDSHAKE_MS + 1);
    CHECK_EQ(g_s.ending, END_FLUSH);
    EXPECT("");
    CHECK(strstr(g_s.why, "no complete handshake within 10 s "
                 "(state WAIT_CR)") != NULL);

    joined();
    rdp_check_time(&g_s, g_s.phase_ms + RDP_HANDSHAKE_MS + 1);
    EXPECT(DPUM_1);
    CHECK(strstr(g_s.why, "(state WAIT_JOINS)") != NULL);

    licensed();
    rdp_check_time(&g_s, g_s.phase_ms + RDP_HANDSHAKE_MS + 1);
    EXPECT(DPUM_1);
    CHECK(strstr(g_s.why, "(state WAIT_CONFIRM)") != NULL);

    active();
    rdp_check_time(&g_s, g_s.phase_ms + 100UL * RDP_HANDSHAKE_MS);
    CHECK_EQ(g_s.ending, END_NO);

    reset();
    g_s.phase_ms = 0xFFFFF000UL;
    rdp_check_time(&g_s, 0x00001000UL);
    CHECK_EQ(g_s.ending, END_NO);
    rdp_check_time(&g_s, 0x00001711UL);
    CHECK_EQ(g_s.ending, END_FLUSH);
}

static unsigned char g_ord[8192];
static unsigned int  g_ord_len, g_ord_n;
static unsigned int  g_bm_n;
static unsigned int  g_bm_x[16], g_bm_y[16];

static void put_order(int kind, long l, long t, long r, long b,
                      unsigned long color, unsigned int rop, int sx, int sy)
{
    CORE_ORDER o;

    memset(&o, 0, sizeof(o));
    o.kind = kind;
    set_rect(&o.rect, l, t, r, b);
    o.color = color;
    o.rop = rop;
    o.src_x = sx;
    o.src_y = sy;
    core_frame_enter();
    core_order(&o);
    core_frame_leave();
}

static int take_update(void)
{
    const unsigned char *p = g_s.tx, *q;
    unsigned int len, at, type, i;

    if (g_s.tx_len < 16) {
        return -1;
    }
    len = (p[2] << 8) | p[3];
    if (len < 30 || len > g_s.tx_len) {
        t_failures++;
        printf("t_proto.c: a packet of %u bytes in a queue of %u\n", len,
               g_s.tx_len);
        g_s.tx_len = 0;
        return -1;
    }
    CHECK(len <= 8192);
    at = (p[13] & 0x80) ? 15 : 14;
    CHECK_EQ(p[at + 14], 2);
    type = get16(p + at + 18);
    if (type == 0) {
        CHECK_EQ(get16(p + at + 20), 0);
        g_ord_n = get16(p + at + 22);
        CHECK_EQ(get16(p + at + 24), 0);
        g_ord_len = len - (at + 26);
        memcpy(g_ord, p + at + 26, g_ord_len);
    }
    if (type == 1) {
        g_bm_n = get16(p + at + 20);
        q = p + at + 22;
        for (i = 0; i < g_bm_n && i < 16; i++) {
            g_bm_x[i] = get16(q);
            g_bm_y[i] = get16(q + 2);
            q += 18 + get16(q + 16);
        }
    }
    memmove(g_s.tx, g_s.tx + len, g_s.tx_len - len);
    g_s.tx_len -= len;
    return (int) type;
}

static void ord_is(unsigned int n, const unsigned char *x, unsigned int len,
                   int line)
{
    if (g_ord_n != n || g_ord_len != len || memcmp(g_ord, x, len) != 0) {
        t_failures++;
        printf("t_proto.c(%d): the Orders update differs (%u orders, %u "
               "bytes; want %u, %u)\n", line, g_ord_n, g_ord_len, n, len);
        print_hex("got", g_ord, g_ord_len);
        print_hex("expected", x, len);
    }
}

static void orders_on(void)
{
    memset(g_s.caps.order_support, 0, sizeof(g_s.caps.order_support));
    g_s.caps.order_support[0] = 1;
    g_s.caps.order_support[1] = 1;
    g_s.caps.order_support[2] = 1;
    g_s.caps.order_support[8] = 1;
    g_s.caps.order_support[10] = 1;
    g_s.caps.order_flags = 0x002A;
    update_screen(&g_s);
    CHECK_EQ(g_s.ord_allow, ORD_A_DSTBLT | ORD_A_PATBLT | ORD_A_SCRBLT |
                            ORD_A_LINETO | ORD_A_OPAQUERECT);
    update_pump(&g_s);
    TAKE(PALETTE);
    drain();
    CHECK_EQ(g_s.map.dirty, 0);
}

static void test_orders(void)
{
    static const unsigned char a1[] = {
        0x09, 0x0A, 0x1F, 0x0A, 0x00, 0x14, 0x00, 0x2C, 0x01, 0x28, 0x00,
        0x07
    };
    static const unsigned char a2[] = { 0x11, 0x02, 0x32 };
    static const unsigned char two[] = { 0x11, 0x02, 0xCE, 0x11, 0x02, 0x32 };
    static const unsigned char scr[] = {
        0x09, 0x02, 0x7F, 0x6E, 0x00, 0x78, 0x00, 0xC8, 0x00, 0x96, 0x00,
        0xCC, 0x64, 0x00, 0x64, 0x00
    };
    unsigned long bitmap, covered;
    unsigned int i;
    RECT r;

    active();
    update_pump(&g_s);
    TAKE(PALETTE);
    drain();
    CHECK_EQ(g_s.ord_allow, 0);
    put_order(CORE_ORDER_FILL, 10, 20, 310, 60, 7, 0xF0, 0, 0);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 1);
    CHECK_EQ(take_update(), -1);
    CHECK_EQ(g_s.stats.ord_bitmap, 1);

    orders_on();
    put_order(CORE_ORDER_FILL, 10, 20, 310, 60, 7, 0xF0, 0, 0);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    ord_is(1, a1, sizeof(a1), __LINE__);
    CHECK_EQ(take_update(), -1);
    put_order(CORE_ORDER_FILL, 10, 70, 310, 110, 7, 0xF0, 0, 0);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    ord_is(1, a2, sizeof(a2), __LINE__);
    put_order(CORE_ORDER_FILL, 10, 20, 310, 60, 7, 0xF0, 0, 0);
    put_order(CORE_ORDER_FILL, 10, 70, 310, 110, 7, 0xF0, 0, 0);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    ord_is(2, two, sizeof(two), __LINE__);
    CHECK_EQ(take_update(), -1);
    put_order(CORE_ORDER_COPY, 110, 120, 310, 270, 0, 0xCC, 100, 100);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    ord_is(1, scr, sizeof(scr), __LINE__);
    CHECK_EQ(g_s.stats.ord_sent, 5);
    CHECK_EQ(g_s.stats.ord_pdus, 4);
    CHECK_EQ(g_s.stats.ord_type[2], 1);
    CHECK_EQ(g_s.stats.ord_type[4], 4);

    bitmap = g_s.stats.ord_bitmap;
    set_rect(&r, 100, 50, 104, 52);
    core_damage(&r, 1);
    put_order(CORE_ORDER_COPY, 200, 300, 210, 310, 0, 0xCC, 98, 48);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 1);
    CHECK_EQ(g_s.stats.ord_type[2], 2);
    CHECK_EQ(take_update(), 1);
    CHECK_EQ(g_bm_n, 2);
    CHECK((g_bm_x[0] == 100 && g_bm_y[0] == 50 && g_bm_x[1] == 200 &&
           g_bm_y[1] == 302) ||
          (g_bm_x[1] == 100 && g_bm_y[1] == 50 && g_bm_x[0] == 200 &&
           g_bm_y[0] == 302));
    CHECK_EQ(take_update(), -1);
    CHECK_EQ(g_s.stats.ord_bitmap, bitmap);
    CHECK_EQ(g_s.map.dirty, 0);
    set_rect(&r, 400, 50, 404, 52);
    core_damage(&r, 1);
    put_order(CORE_ORDER_COPY, 200, 300, 210, 310, 0, 0xCC, 98, 48);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(take_update(), 1);
    CHECK_EQ(g_bm_n, 1);
    CHECK_EQ(g_bm_x[0], 400);
    CHECK_EQ(take_update(), -1);
    put_order(CORE_ORDER_FILL, 0, 700, 64, 710, 3, 0x5A, 0, 0);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 1);
    CHECK_EQ(g_s.stats.ord_bitmap, bitmap + 1);
    set_rect(&r, 100, 50, 104, 52);
    core_damage(&r, 1);
    rdp_take_damage(&g_s);
    g_s.map.sent[1] = 0;
    g_s.map.unsent = 1;
    put_order(CORE_ORDER_COPY, 200, 300, 210, 310, 0, 0xCC, 98, 48);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 1);
    CHECK_EQ(g_bm_n, 2);
    CHECK_EQ(take_update(), -1);
    CHECK_EQ(g_s.stats.ord_bitmap, bitmap + 2);
    CHECK_EQ(g_s.map.unsent, 0);
    CHECK_EQ(g_s.map.dirty, 0);
    covered = g_s.stats.ord_covered;
    set_rect(&r, 0, 0, 64, 64);
    core_damage(&r, 1);
    put_order(CORE_ORDER_FILL, 8, 8, 40, 40, 3, 0xF0, 0, 0);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 1);
    CHECK_EQ(take_update(), -1);
    CHECK_EQ(g_s.stats.ord_covered, covered + 1);
    for (i = 0; i <= CORE_ORDER_LOG; i++) {
        put_order(CORE_ORDER_FILL, 8, 8, 40, 40, 3, 0xF0, 0, 0);
    }
    clear_screen();
    rdp_pump(&g_s);
    CHECK_EQ(g_s.stats.ord_lost, 1);
    take_bitmaps();
    drain();
    CHECK_EQ(seen_count(), 1024UL * 768UL);
    CHECK_EQ(picture_crc(), PICTURE_CRC);
    CHECK_EQ(g_s.ending, END_NO);

    put_order(CORE_ORDER_FILL, 10, 20, 310, 60, 7, 0xF0, 0, 0);
    rdp_take_damage(&g_s);
    core_frame_enter();
    g_s.ord_pdu.count = 1;
    g_s.ord_pdu.len = 3;
    core_frame_leave();
    CHECK(!g_s.ord.fresh);
    CHECK(caps_demand_active(&g_s));
    CHECK(g_s.ord.fresh);
    CHECK_EQ(g_s.ord.last_type, ORD_PATBLT);
    CHECK_EQ(g_s.ord_pdu.count, 0);
    CHECK_EQ(g_s.ord_pdu.len, 0);
    g_s.tx_len = 0;

    active_raw();
    pointer_pump(&g_s);
    TAKE(PTR_DEFAULT);
    orders_on();
    set_rect(&r, 0, 0, 1024, 128);
    core_damage(&r, 1);
    rdp_take_damage(&g_s);
    g_s.tx_len = UPDATE_QUEUE_MAX - 1;
    update_pump(&g_s);
    CHECK(g_s.tile.ready);
    g_s.tx_len = 0;
    put_order(CORE_ORDER_FILL, 0, 700, 100, 710, 5, 0xF0, 0, 0);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 1);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 1);
    CHECK_EQ(take_update(), 1);
    g_s.tx_len = 0;
    drain();
    CHECK_EQ(g_s.map.dirty, 0);
    CHECK_EQ(g_s.ending, END_NO);
}

static void test_invert(void)
{
    unsigned long bitmap, dst;
    unsigned int i;

    active();
    update_pump(&g_s);
    TAKE(PALETTE);
    drain();
    orders_on();
    bitmap = g_s.stats.ord_bitmap;
    dst = g_s.stats.ord_type[0];
    put_order(CORE_ORDER_FILL, 480, 352, 482, 368, 15, 0x5A, 0, 0);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 1);
    CHECK_EQ(g_ord[1], ORD_DSTBLT);
    CHECK_EQ(g_ord[g_ord_len - 1], 0x55);
    CHECK_EQ(take_update(), -1);
    CHECK_EQ(g_s.stats.ord_type[0], dst + 1);
    put_order(CORE_ORDER_FILL, 490, 352, 492, 368, 0, 0x55, 0, 0);
    put_order(CORE_ORDER_FILL, 0, 700, 64, 710, 0, 0x55, 0, 0);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 2);
    CHECK_EQ(take_update(), -1);
    CHECK_EQ(g_s.stats.ord_type[0], dst + 3);
    CHECK_EQ(g_s.stats.ord_bitmap, bitmap);
    put_order(CORE_ORDER_FILL, 480, 352, 482, 368, 3, 0x5A, 0, 0);
    put_order(CORE_ORDER_FILL, 200, 700, 202, 716, 0, 0x55, 0, 0);
    rdp_pump(&g_s);
    while (take_update() >= 0) {
    }
    CHECK_EQ(g_s.stats.ord_bitmap, bitmap + 2);
    CHECK_EQ(g_s.stats.ord_type[0], dst + 3);
    put_order(CORE_ORDER_FILL, 480, 352, 482, 368, 15, 0x5A, 0, 0);
    put_order(CORE_ORDER_COPY, 600, 100, 610, 110, 0, 0xCC, 476, 360);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 1);
    while (take_update() >= 0) {
    }
    CHECK_EQ(g_s.stats.ord_bitmap, bitmap + 3);
    CHECK_EQ(g_s.stats.ord_type[0], dst + 3);
    put_order(CORE_ORDER_FILL, 480, 352, 482, 368, 15, 0x5A, 0, 0);
    put_order(CORE_ORDER_FILL, 470, 360, 490, 362, 7, 0x5A, 0, 0);
    rdp_pump(&g_s);
    while (take_update() >= 0) {
    }
    CHECK_EQ(g_s.stats.ord_bitmap, bitmap + 5);
    CHECK_EQ(g_s.stats.ord_type[0], dst + 3);
    put_order(CORE_ORDER_FILL, 480, 352, 482, 368, 15, 0x5A, 0, 0);
    put_order(CORE_ORDER_SAVE, 400, 300, 500, 400, 0, 0, 0, 0);
    rdp_pump(&g_s);
    while (take_update() >= 0) {
    }
    CHECK_EQ(g_s.stats.ord_bitmap, bitmap + 6);
    CHECK_EQ(g_s.stats.ord_type[0], dst + 3);
    put_order(CORE_ORDER_FILL, 480, 352, 482, 368, 15, 0x5A, 0, 0);
    put_order(CORE_ORDER_COPY, 500, 380, 510, 396, 0, 0xCC, 480, 352);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 2);
    CHECK_EQ(take_update(), -1);
    CHECK_EQ(g_s.stats.ord_bitmap, bitmap + 6);
    CHECK_EQ(g_s.stats.ord_type[0], dst + 4);
    put_order(CORE_ORDER_FILL, 480, 352, 482, 368, 15, 0x5A, 0, 0);
    put_order(CORE_ORDER_COPY, 500, 380, 510, 396, 0, 0xCC, 480, 352);
    put_order(CORE_ORDER_COPY, 300, 300, 310, 310, 0, 0xCC, 500, 380);
    rdp_pump(&g_s);
    while (take_update() >= 0) {
    }
    CHECK_EQ(g_s.stats.ord_bitmap, bitmap + 7);
    CHECK_EQ(g_s.stats.ord_type[0], dst + 4);
    put_order(CORE_ORDER_FILL, 480, 352, 482, 368, 15, 0x5A, 0, 0);
    put_order(CORE_ORDER_FILL, 481, 360, 500, 362, 15, 0xF0, 0, 0);
    put_order(CORE_ORDER_COPY, 470, 352, 480, 368, 0, 0xCC, 100, 100);
    put_order(CORE_ORDER_FILL, 480, 352, 482, 368, 15, 0x5A, 0, 0);
    put_order(CORE_ORDER_FILL, 10, 700, 20, 710, 7, 0xF0, 0, 0);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 5);
    CHECK_EQ(take_update(), -1);
    CHECK_EQ(g_s.stats.ord_bitmap, bitmap + 7);
    CHECK_EQ(g_s.stats.ord_type[0], dst + 6);
    put_order(CORE_ORDER_FILL, 480, 352, 482, 368, 15, 0x5A, 0, 0);
    for (i = 0; i < 40; i++) {
        put_order(CORE_ORDER_FILL, 10, 700, 20, 710, 7, 0xF0, 0, 0);
    }
    rdp_pump(&g_s);
    while (take_update() >= 0) {
    }
    CHECK_EQ(g_s.stats.ord_bitmap, bitmap + 8);
    CHECK_EQ(g_s.stats.ord_type[0], dst + 6);
    drain();
    CHECK_EQ(g_s.map.dirty, 0);
    CHECK_EQ(g_s.ending, END_NO);
}

static void put_save(int kind, long l, long t, long r, long b,
                     unsigned long num)
{
    CORE_ORDER o;

    memset(&o, 0, sizeof(o));
    o.kind = kind;
    set_rect(&o.rect, l, t, r, b);
    o.save = num;
    core_frame_enter();
    core_order(&o);
    core_frame_leave();
}

static void test_seq(void)
{
    unsigned long saves, restores, covered;
    RECT r;

    active();
    update_pump(&g_s);
    TAKE(PALETTE);
    drain();
    memset(g_s.caps.order_support, 0, sizeof(g_s.caps.order_support));
    g_s.caps.order_support[2] = 1;
    g_s.caps.order_support[10] = 1;
    g_s.caps.order_support[11] = 1;
    g_s.caps.order_flags = 0x002A;
    g_s.caps.desktop_save_size = ORD_SAVE_MAX;
    update_screen(&g_s);
    CHECK_EQ(g_s.ord_allow, ORD_A_SCRBLT | ORD_A_OPAQUERECT |
                            ORD_A_SAVEBITMAP);
    CHECK_EQ(g_s.save.size, ORD_SAVE_MAX);
    update_pump(&g_s);
    TAKE(PALETTE);
    drain();
    CHECK_EQ(g_s.map.dirty, 0);
    saves = g_s.save.saves;
    restores = g_s.save.restores;
    covered = g_s.stats.ord_covered;

    put_save(CORE_ORDER_SAVE, 400, 300, 500, 400, 1);
    put_order(CORE_ORDER_FILL, 400, 300, 500, 302, 0, 0xF0, 0, 0);
    set_rect(&r, 420, 310, 440, 318);
    core_damage(&r, 1);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 2);
    CHECK_EQ(take_update(), 1);
    CHECK_EQ(g_bm_n, 1);
    CHECK_EQ(take_update(), -1);
    CHECK_EQ(g_s.save.n, 1);
    CHECK_EQ(g_s.save.e[0].stale.right, 0);
    put_save(CORE_ORDER_RESTORE, 400, 300, 500, 400, 1);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 1);
    CHECK_EQ(take_update(), -1);
    CHECK_EQ(g_s.save.saves, saves + 1);
    CHECK_EQ(g_s.save.restores, restores + 1);
    core_damage(&r, 1);
    put_save(CORE_ORDER_SAVE, 400, 300, 500, 400, 2);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(take_update(), 1);
    CHECK_EQ(take_update(), -1);
    CHECK(g_s.save.e[0].stale.right > 0);
    put_save(CORE_ORDER_RESTORE, 400, 300, 500, 400, 2);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(take_update(), 1);
    CHECK_EQ(g_bm_n, 1);
    CHECK_EQ(g_bm_x[0], 420);
    CHECK_EQ(g_bm_y[0], 310);
    CHECK_EQ(take_update(), -1);
    CHECK_EQ(g_s.save.restores, restores + 2);

    put_order(CORE_ORDER_COPY, 300, 300, 310, 310, 0, 0xCC, 100, 100);
    set_rect(&r, 100, 100, 110, 110);
    core_damage(&r, 1);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 1);
    CHECK_EQ(take_update(), 1);
    CHECK_EQ(g_bm_n, 1);
    CHECK_EQ(g_bm_x[0], 100);
    CHECK_EQ(g_bm_y[0], 100);
    CHECK_EQ(take_update(), -1);
    core_damage(&r, 1);
    put_order(CORE_ORDER_COPY, 300, 300, 310, 310, 0, 0xCC, 100, 100);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(take_update(), 1);
    CHECK_EQ(g_bm_n, 2);
    CHECK_EQ(take_update(), -1);

    put_order(CORE_ORDER_FILL, 100, 100, 110, 110, 7, 0xF0, 0, 0);
    put_order(CORE_ORDER_COPY, 300, 300, 310, 310, 0, 0xCC, 100, 100);
    set_rect(&r, 96, 96, 120, 120);
    core_damage(&r, 1);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 1);
    CHECK_EQ(take_update(), 1);
    CHECK_EQ(g_bm_n, 2);
    CHECK_EQ(take_update(), -1);
    CHECK_EQ(g_s.stats.ord_covered, covered + 1);
    put_save(CORE_ORDER_SAVE, 400, 300, 500, 400, 3);
    put_save(CORE_ORDER_RESTORE, 400, 300, 500, 400, 3);
    put_order(CORE_ORDER_COPY, 300, 100, 400, 200, 0, 0xCC, 400, 300);
    set_rect(&r, 400, 300, 500, 400);
    core_damage(&r, 1);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 2);
    clear_screen();
    take_bitmaps();
    CHECK_EQ(g_seen[150][350], 1);
    CHECK_EQ(g_s.stats.ord_covered, covered + 2);
    CHECK_EQ(g_s.save.n, 0);
    drain();
    CHECK_EQ(g_s.map.dirty, 0);
    CHECK_EQ(g_s.ending, END_NO);
}

static CORE_GLYPH g_gh, g_gi;
static CORE_GLYPH g_gx[8];

static void make_glyphs(void)
{
    static const unsigned char h[] = { 0x84, 0x84, 0x84, 0xFC, 0x84, 0x84,
                                       0x84 };
    static const unsigned char i[] = { 0x40, 0x00, 0xC0, 0x40, 0x40, 0x40,
                                       0x40 };
    int k;

    memset(&g_gh, 0, sizeof(g_gh));
    g_gh.oy = -7;
    g_gh.cx = 6;
    g_gh.cy = 7;
    memcpy(g_gh.bits, h, sizeof(h));
    g_gi = g_gh;
    g_gi.cx = 2;
    memcpy(g_gi.bits, i, sizeof(i));
    for (k = 0; k < 8; k++) {
        memset(&g_gx[k], 0, sizeof(g_gx[k]));
        g_gx[k].oy = -7;
        g_gx[k].cx = 8;
        g_gx[k].cy = 7;
        g_gx[k].bits[0] = (unsigned char) (0x80 >> k);
        g_gx[k].bits[6] = 0xFF;
    }
}

static void put_text(int n, const CORE_GLYPH *const *g, const int *x, int y,
                     long l, long t, long r, long b, const RECT *op,
                     unsigned long fg, unsigned long bg)
{
    CORE_GLYPHREF refs[CORE_TEXT_MAX];
    CORE_ORDER o;
    int i;

    core_frame_enter();
    for (i = 0; i < n; i++) {
        refs[i].id = (unsigned int) core_glyph_put(g[i], &refs[i].serial);
        refs[i].x = x[i];
    }
    memset(&o, 0, sizeof(o));
    o.kind = CORE_ORDER_TEXT;
    set_rect(&o.rect, l, t, r, b);
    if (op != NULL) {
        o.op = *op;
    }
    o.y = y;
    o.color = fg;
    o.bg = bg;
    o.nglyph = n;
    o.run = core_text_put(refs, n);
    core_order(&o);
    core_frame_leave();
}

static void put_hi(void)
{
    static const int x[2] = { 100, 107 };
    const CORE_GLYPH *g[2];
    RECT op;

    g[0] = &g_gh;
    g[1] = &g_gi;
    set_rect(&op, 100, 43, 110, 51);
    put_text(2, g, x, 50, 100, 43, 110, 51, &op, 0, 15);
}

static void text_on(int fast, int level, unsigned int entries2)
{
    static const unsigned int cell[10] = { 4, 4, 8, 8, 16, 32, 64, 128,
                                           256, 2048 };
    unsigned char *p = g_s.caps.glyph;
    unsigned int c, e;

    memset(p, 0, sizeof(g_s.caps.glyph));
    for (c = 0; c < 10; c++) {
        e = c == 9 ? 64 : c == 2 ? entries2 : 254;
        p[4 * c] = (unsigned char) e;
        p[4 * c + 1] = (unsigned char) (e >> 8);
        p[4 * c + 2] = (unsigned char) cell[c];
        p[4 * c + 3] = (unsigned char) (cell[c] >> 8);
    }
    p[44] = (unsigned char) level;
    g_s.caps.have |= 1UL << CAPS_GLYPH;
    memset(g_s.caps.order_support, 0, sizeof(g_s.caps.order_support));
    g_s.caps.order_support[10] = 1;
    g_s.caps.order_support[fast ? 19 : 27] = 1;
    g_s.caps.order_flags = 0x002A;
    update_screen(&g_s);
    update_pump(&g_s);
    TAKE(PALETTE);
    drain();
    CHECK_EQ(g_s.map.dirty, 0);
}

static void test_text(void)
{
    static const unsigned char b6b8[] = {
        0x03, 0x1F, 0x00, 0x00, 0x00, 0x03, 0x02, 0x02,
        0x00, 0x00, 0x00, 0x00, 0xF9, 0xFF, 0x06, 0x00, 0x07, 0x00,
        0x84, 0x84, 0x84, 0xFC, 0x84, 0x84, 0x84, 0x00,
        0x01, 0x00, 0x00, 0x00, 0xF9, 0xFF, 0x02, 0x00, 0x07, 0x00,
        0x40, 0x00, 0xC0, 0x40, 0x40, 0x40, 0x40, 0x00,
        0x09, 0x13, 0xFB, 0x7A, 0x02, 0x00, 0x02, 0x0F, 0x00, 0x00,
        0x64, 0x00, 0x2B, 0x00, 0x6E, 0x00, 0x33, 0x00, 0x0F, 0x00,
        0x00, 0x80, 0x00, 0x80, 0x32, 0x00, 0x04, 0x00, 0x00, 0x01, 0x07
    };
    static const unsigned char b7[] = {
        0x19, 0x1B, 0xE3, 0x3F, 0x38, 0x02, 0x02, 0x0F, 0x00, 0x00,
        0x64, 0x00, 0x2B, 0x00, 0x6E, 0x00, 0x33, 0x00,
        0x64, 0x00, 0x2B, 0x00, 0x6E, 0x00, 0x33, 0x00,
        0x64, 0x00, 0x32, 0x00, 0x04, 0x00, 0x00, 0x01, 0x07
    };
    static const unsigned char again[] = { 0x91 };
    static const unsigned char hi_fills[] = {
        0x19, 0x0A, 0x1F, 0x64, 0x32, 0x0A, 0x01, 0x0F,
        0x11, 0x0F, 0x09, 0xF9, 0xF7, 0x06
    };
    static const int hi_x[2] = { 100, 107 };
    const CORE_GLYPH *g[CORE_TEXT_MAX];
    CORE_GLYPH big[2][CORE_TEXT_MAX];
    int x[CORE_TEXT_MAX], i, k;
    unsigned long bitmap, sent, fringe;
    unsigned int pdus;
    CORE_GLYPH z;
    RECT wide;

    make_glyphs();

    active();
    text_on(1, 2, 254);
    CHECK_EQ(g_s.ord_allow, ORD_A_OPAQUERECT | ORD_A_FASTINDEX);
    CHECK(g_s.glyph.usable);
    CHECK_EQ(g_s.glyph.level, 2);
    put_hi();
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    ord_is(2, b6b8, sizeof(b6b8), __LINE__);
    CHECK_EQ(take_update(), -1);
    put_hi();
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    ord_is(1, again, sizeof(again), __LINE__);
    CHECK_EQ(g_s.glyph.fills, 1);
    CHECK_EQ(g_s.glyph.glyphs, 2);
    CHECK_EQ(g_s.stats.ord_type[5], 2);
    CHECK_EQ(g_s.ending, END_NO);

    active();
    text_on(0, 2, 254);
    CHECK_EQ(g_s.ord_allow, ORD_A_OPAQUERECT | ORD_A_GLYPHINDEX);
    put_hi();
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 2);
    CHECK_EQ(g_ord_len, 44 + sizeof(b7));
    CHECK_MEM(g_ord, b6b8, 44);
    CHECK_MEM(g_ord + 44, b7, sizeof(b7));

    active();
    text_on(1, 2, 254);
    g[0] = &g_gh;
    g[1] = &g_gi;
    set_rect(&wide, 100, 43, 32772, 51);
    sent = g_s.glyph.why[GLYPHS_W_CODE];
    put_text(2, g, hi_x, 50, 100, 43, 110, 51, &wide, 0, 15);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    ord_is(2, b6b8, sizeof(b6b8), __LINE__);
    CHECK_EQ(take_update(), -1);
    CHECK_EQ(g_s.glyph.why[GLYPHS_W_CODE], sent);

    active();
    text_on(1, 0, 254);
    CHECK_EQ(g_s.ord_allow, ORD_A_OPAQUERECT);
    bitmap = g_s.stats.ord_bitmap;
    fringe = g_s.stats.ord_fringe;
    put_hi();
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    ord_is(2, hi_fills, sizeof(hi_fills), __LINE__);
    CHECK_EQ(take_update(), 1);
    CHECK_EQ(g_bm_n, 1);
    CHECK_EQ(g_bm_x[0], 100);
    CHECK_EQ(g_bm_y[0], 43);
    CHECK_EQ(take_update(), -1);
    CHECK_EQ(g_s.stats.ord_bitmap, bitmap);
    CHECK_EQ(g_s.stats.ord_fringe, fringe + 1);
    g[0] = &g_gh;
    g[1] = &g_gi;
    set_rect(&wide, 100, 43, 130, 51);
    put_text(2, g, hi_x, 50, 120, 43, 130, 51, &wide, 0, 15);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 1);
    CHECK_EQ(take_update(), -1);
    CHECK_EQ(g_s.stats.ord_fringe, fringe + 2);
    put_text(2, g, hi_x, 50, 100, 43, 109, 50, NULL, 0, 15);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 1);
    CHECK_EQ(take_update(), -1);
    CHECK_EQ(g_s.stats.ord_bitmap, bitmap + 1);
    CHECK_EQ(g_s.stats.ord_fringe, fringe + 2);
    g_s.caps.have &= ~(1UL << CAPS_GLYPH);
    update_screen(&g_s);
    CHECK_EQ(g_s.ord_allow, ORD_A_OPAQUERECT);
    CHECK(!g_s.glyph.usable);
    g_s.tx_len = 0;

    active();
    text_on(1, 2, 4);
    for (k = 0; k < 5; k++) {
        x[k] = 100 + 10 * k;
    }
    for (k = 0; k < 3; k++) {
        g[k] = &g_gx[k];
    }
    put_text(3, g, x, 50, 100, 43, 130, 51, NULL, 0, 15);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 2);
    CHECK_EQ(g_ord[6], 2);
    CHECK_EQ(g_ord[7], 3);
    g[0] = &g_gx[3];
    put_text(1, g, x, 50, 100, 43, 130, 51, NULL, 0, 15);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord[8], 3);
    g[0] = &g_gx[0];
    put_text(1, g, x, 50, 100, 43, 130, 51, NULL, 0, 15);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 1);
    g[0] = &g_gx[4];
    put_text(1, g, x, 50, 100, 43, 130, 51, NULL, 0, 15);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 2);
    CHECK_EQ(g_ord[8], 1);
    g[0] = &g_gx[1];
    put_text(1, g, x, 50, 100, 43, 130, 51, NULL, 0, 15);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord_n, 2);
    CHECK_EQ(g_ord[8], 2);
    for (k = 0; k < 5; k++) {
        g[k] = &g_gx[k + 3];
    }
    put_text(5, g, x, 50, 100, 43, 150, 51, NULL, 0, 15);
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 0);
    CHECK_EQ(g_ord[6], 3);
    CHECK_EQ(g_ord[7], 5);
    CHECK_EQ(take_update(), -1);

    put_hi();
    core_frame_enter();
    memset(&z, 0, sizeof(z));
    for (i = 0; i < CORE_GLYPH_PLACES; i++) {
        unsigned long serial;

        z.cx = 1 + i % 32;
        z.cy = 1 + i / 32;
        core_glyph_put(&z, &serial);
    }
    core_frame_leave();
    bitmap = g_s.stats.ord_bitmap;
    rdp_pump(&g_s);
    CHECK_EQ(take_update(), 1);
    CHECK_EQ(take_update(), -1);
    CHECK_EQ(g_s.stats.ord_bitmap, bitmap + 1);

    for (k = 0; k < 2; k++) {
        for (i = 0; i < 40; i++) {
            memset(&big[k][i], 0, sizeof(big[k][i]));
            big[k][i].oy = -32;
            big[k][i].cx = 32;
            big[k][i].cy = 32;
            big[k][i].bits[0] = (unsigned char) i;
            big[k][i].bits[1] = (unsigned char) k;
            big[k][i].bits[127] = 1;
            g[i] = &big[k][i];
            x[i] = 32 * i;
        }
        put_text(40, g, x, 400 + 40 * k, 0, 0, 1024, 768, NULL, 255, 0);
    }
    sent = g_s.stats.ord_sent;
    pdus = 0;
    rdp_pump(&g_s);
    while ((i = take_update()) == 0) {
        pdus++;
        CHECK(g_ord_len <= ORDER_PDU_BYTES);
        rdp_pump(&g_s);
    }
    CHECK_EQ(i, -1);
    CHECK_EQ(pdus, 2);
    CHECK_EQ(g_s.stats.ord_sent, sent + 5);
    CHECK_EQ(g_s.ending, END_NO);
}

static void test_palette_capture(void)
{
    static const unsigned char st[20][3] = {
        { 0x00, 0x00, 0x00 }, { 0x80, 0x00, 0x00 }, { 0x00, 0x80, 0x00 },
        { 0x80, 0x80, 0x00 }, { 0x00, 0x00, 0x80 }, { 0x80, 0x00, 0x80 },
        { 0x00, 0x80, 0x80 }, { 0xC0, 0xC0, 0xC0 }, { 0xC0, 0xDC, 0xC0 },
        { 0xA6, 0xCA, 0xF0 }, { 0xFF, 0xFB, 0xF0 }, { 0xA0, 0xA0, 0xA4 },
        { 0x80, 0x80, 0x80 }, { 0xFF, 0x00, 0x00 }, { 0x00, 0xFF, 0x00 },
        { 0xFF, 0xFF, 0x00 }, { 0x00, 0x00, 0xFF }, { 0xFF, 0x00, 0xFF },
        { 0x00, 0xFF, 0xFF }, { 0xFF, 0xFF, 0xFF }
    };
    static unsigned char pal[256][3], old[256][3];
    int i, k, changed;

    for (i = 0; i < 256; i++) {
        pal[i][0] = (unsigned char) ((i & 7) * 32);
        pal[i][1] = (unsigned char) (((i >> 3) & 7) * 32);
        pal[i][2] = (unsigned char) ((i >> 6) * 64);
    }
    for (i = 0; i < 10; i++) {
        for (k = 0; k < 3; k++) {
            pal[i][k] = st[i][k];
            pal[246 + i][k] = st[10 + i][k];
        }
    }
    CHECK_EQ(pal[164][0], 0x80);
    CHECK_EQ(pal[164][1], 0x80);
    CHECK_EQ(pal[164][2], 0x80);
    memcpy(old, pal, sizeof(old));
    CHECK_EQ(palette_capture(pal), 6);
    for (i = 0; i < 256; i++) {
        changed = i == 32 || i == 36 || i == 128 || i == 132 || i == 160 ||
                  i == 164;
        CHECK_EQ(pal[i][0], old[i][0]);
        CHECK_EQ(pal[i][1], old[i][1]);
        CHECK_EQ(pal[i][2], changed ? old[i][2] ^ 1 : old[i][2]);
    }
    CHECK_EQ(palette_capture(pal), 0);
}

int main(void)
{
    void *core = core_start(&testpat_backend);

    rdp_start(&core_api, core);
    test_x224();
    test_connect();
    test_domain();
    test_info();
    test_sdin();
    test_compress();
    test_finalize();
    test_depth();
    test_screen();
    test_raw();
    test_palette_change();
    test_update_row();
    test_map();
    test_damage_list();
    test_pointer();
    test_decode();
    test_client_pdus();
    test_prompt();
    test_reduce();
    test_reactivate();
    test_second();
    test_timeout();
    test_orders();
    test_invert();
    test_seq();
    test_text();
    test_palette_capture();
    rdp_done(&g_s);
    core_stop(core);
    return t_result("t_proto");
}
