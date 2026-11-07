/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mppc.h"
#include "tcheck.h"

static unsigned int hex_to(const char *hex, unsigned char *out)
{
    unsigned int n = 0, v;

    while (*hex) {
        if (*hex == ' ') {
            hex++;
            continue;
        }
        sscanf(hex, "%2x", &v);
        out[n++] = (unsigned char) v;
        hex += 2;
    }
    return n;
}

static void bits_str(const unsigned char *p, int nbits, char *out)
{
    int i;

    for (i = 0; i < nbits; i++) {
        out[i] = (char) ((p[i / 8] & (0x80 >> (i % 8))) ? '1' : '0');
    }
    out[nbits] = 0;
}

static unsigned long g_seed = 12345;

static unsigned int rnd(void)
{
    g_seed = g_seed * 1103515245UL + 12345UL;
    return (unsigned int) ((g_seed >> 8) & 0xFFFFFF);
}

static void fill_text(unsigned char *p, unsigned int n)
{
    unsigned int i, back, alpha = 2 + rnd() % 30;

    for (i = 0; i < n; i++) {
        if (i > 40 && rnd() % 3 == 0) {
            back = 1 + rnd() % (i < 300 ? i : 300);
            p[i] = p[i - back];
        } else {
            p[i] = (unsigned char) (0x20 + rnd() % alpha +
                                    (rnd() % 5 == 0 ? 0x80 : 0));
        }
    }
}

static void fill_words(unsigned char *p, unsigned int n)
{
    static const char *words[] = {
        "the ", "console ", "of ", "Windows NT ", "draws ", "a ", "window ",
        "and ", "ntrdps ", "sends ", "it ", "to ", "client ", "\xB0\xB1 ",
        "orders ", "bitmaps ", "glyphs ", "\r\n"
    };
    unsigned int i = 0;
    const char *w;

    while (i < n) {
        w = words[rnd() % (sizeof(words) / sizeof(words[0]))];
        while (*w && i < n) {
            p[i++] = (unsigned char) *w++;
        }
    }
}

static void fill_random(unsigned char *p, unsigned int n)
{
    unsigned int i;

    for (i = 0; i < n; i++) {
        p[i] = (unsigned char) (rnd() >> 4);
    }
}

typedef struct {
    unsigned char hist[65536];
    unsigned int  pos;
    unsigned int  hsize;
} DEC;

typedef struct {
    const unsigned char *p;
    unsigned long bit, nbits;
} BR;

static unsigned int get(BR *r, int count, int *err)
{
    unsigned int v = 0;

    while (count-- > 0) {
        if (r->bit >= r->nbits) {
            *err = 1;
            return 0;
        }
        v = (v << 1) | ((r->p[r->bit / 8] >> (7 - r->bit % 8)) & 1);
        r->bit++;
    }
    return v;
}

static void dec_init(DEC *d, int type)
{
    memset(d->hist, 0, sizeof(d->hist));
    d->pos = 0;
    d->hsize = type ? 65536u : 8192u;
}

static long dec_body(DEC *d, int type, unsigned int flags,
                     const unsigned char *in, unsigned int len,
                     unsigned int *start)
{
    BR r;
    int err = 0, c, k;
    unsigned int off, n, v;

    if (flags & MPPC_FLUSHED) {
        memset(d->hist, 0, sizeof(d->hist));
        d->pos = 0;
    }
    if (flags & MPPC_AT_FRONT) {
        d->pos = 0;
    }
    *start = d->pos;
    r.p = in;
    r.bit = 0;
    r.nbits = (unsigned long) len * 8;
    while (r.nbits - r.bit >= 8) {
        if (get(&r, 1, &err) == 0) {
            v = get(&r, 7, &err);
        } else if (get(&r, 1, &err) == 0) {
            v = 0x80 + get(&r, 7, &err);
        } else {
            if (get(&r, 1, &err) == 0) {
                off = type ? 2368 + get(&r, 16, &err)
                           : 320 + get(&r, 13, &err);
            } else if (get(&r, 1, &err) == 0) {
                off = type ? 320 + get(&r, 11, &err)
                           : 64 + get(&r, 8, &err);
            } else if (type == 0) {
                off = get(&r, 6, &err);
            } else if (get(&r, 1, &err) == 0) {
                off = 64 + get(&r, 8, &err);
            } else {
                off = get(&r, 6, &err);
            }
            c = 0;
            while (get(&r, 1, &err) == 1 && !err) {
                c++;
            }
            if (c == 0) {
                n = 3;
            } else {
                k = c + 1;
                n = (1u << k) + get(&r, k, &err);
            }
            if (err || off == 0 || off > d->pos || d->pos + n > d->hsize) {
                return -1;
            }
            while (n-- > 0) {
                d->hist[d->pos] = d->hist[d->pos - off];
                d->pos++;
            }
            continue;
        }
        if (err || d->pos >= d->hsize) {
            return -1;
        }
        d->hist[d->pos++] = (unsigned char) v;
    }
    if (r.bit < r.nbits && get(&r, (int) (r.nbits - r.bit), &err) != 0) {
        return -1;
    }
    return (long) (d->pos - *start);
}

static void check_copy(int type, unsigned int off, unsigned int len,
                       const char *want)
{
    unsigned char buf[8];
    char got[64];
    int n;

    n = mppc_copy_bits(type, off, len, buf, sizeof(buf));
    if (n < 0) {
        CHECK(n >= 0);
        return;
    }
    bits_str(buf, n, got);
    if (strcmp(got, want) != 0) {
        t_failures++;
        printf("t_mppc: type %d copy(%u, %u): %s, want %s\n", type, off, len,
               got, want);
    }
}

static const struct {
    unsigned int off;
    const char *t0, *t1;
} g_offs[] = {
    { 1,     "1111000001",       "11111000001" },
    { 63,    "1111111111",       "11111111111" },
    { 64,    "111000000000",     "1111000000000" },
    { 319,   "111011111111",     "1111011111111" },
    { 320,   "1100000000000000", "111000000000000" },
    { 2367,  "1100011111111111", "111011111111111" },
    { 2368,  "1100100000000000", "1100000000000000000" },
    { 8191,  "1101111010111111", "1100001011010111111" },
    { 65535, NULL,               "1101111011010111111" }
};

static const struct {
    unsigned int len;
    const char *bits;
    int t1_only;
} g_lens[] = {
    { 3, "0", 0 }, { 4, "1000", 0 }, { 7, "1011", 0 }, { 8, "110000", 0 },
    { 15, "110111", 0 }, { 16, "11100000", 0 }, { 63, "1111011111", 0 },
    { 64, "111110000000", 0 }, { 255, "11111101111111", 0 },
    { 256, "1111111000000000", 0 },
    { 4095, "1111111111011111111111", 0 },
    { 4096, "111111111110000000000000", 0 },
    { 8191, "111111111110111111111111", 0 },
    { 8192, "11111111111100000000000000", 1 },
    { 16384, "1111111111111000000000000000", 1 },
    { 32768, "111111111111110000000000000000", 1 }
};

static void test_codes(void)
{
    char want[80];
    unsigned int i;
    int type;

    for (i = 0; i < sizeof(g_offs) / sizeof(g_offs[0]); i++) {
        if (g_offs[i].t0 != NULL) {
            sprintf(want, "%s0", g_offs[i].t0);
            check_copy(0, g_offs[i].off, 3, want);
        }
        sprintf(want, "%s0", g_offs[i].t1);
        check_copy(1, g_offs[i].off, 3, want);
    }
    for (i = 0; i < sizeof(g_lens) / sizeof(g_lens[0]); i++) {
        for (type = g_lens[i].t1_only; type <= 1; type++) {
            sprintf(want, "%s%s", type ? "11111000001" : "1111000001",
                    g_lens[i].bits);
            check_copy(type, 1, g_lens[i].len, want);
        }
    }
}

static void check_body(MPPC_ENC *e, const unsigned char *in, unsigned int n,
                       unsigned int want_flags, const char *want_hex,
                       const char *name)
{
    unsigned char want[200];
    const unsigned char *out = NULL;
    unsigned int flags, outlen = 0, wn;

    flags = mppc_compress(e, in, n, &out, &outlen);
    wn = want_hex ? hex_to(want_hex, want) : 0;
    if (flags != want_flags || (flags != 0 &&
        (outlen != wn || memcmp(out, want, wn) != 0))) {
        t_failures++;
        printf("t_mppc: %s type %d: flags 0x%02X (want 0x%02X), %u bytes "
               "(want %u)\n", name, e->type, flags, want_flags, outlen, wn);
    }
}

static void test_vectors(void)
{
    static const char *v2[2] = { "61 62 63 F0 F1", "61 62 63 F8 78 80" };
    static const char *v4a[2] = { "41 42 43 44 45 46 47 48 F2 30",
                                  "41 42 43 44 45 46 47 48 F9 18 00" };
    static const char *v4b[2] = { "F3 30 21", "F9 98 10 80" };
    static const char *v6[2] = { "00 F0 7C 70", "00 F8 3E 38" };
    static const char *v7[2] = { "BF F8 36", "BF FC 1B" };
    static const char *v3t[2] = { "E0 68", "F0 34 00" };
    static const char *pb[2] = {
        "01 00 01 00 F0 60 3C 00 0C 00 40 00 10 00 23 CD 20 00 03 F8 3E F8",
        "01 00 01 00 F8 30 1E 00 06 00 20 00 08 00 11 F3 48 00 00 FF 07 DF" };
    unsigned char in[200], v3[200], body[86];
    char hex[600];
    MPPC_ENC *e;
    unsigned int i;
    int t;

    for (t = 0; t <= 1; t++) {
        e = mppc_new(t);
        CHECK(e != NULL);
        if (e == NULL) {
            return;
        }
        check_body(e, (const unsigned char *) "\x41\x42\x80\xFF\x7F", 5, 0,
                   NULL, "V1");
        CHECK_EQ(e->pos, 0);
        check_body(e, (const unsigned char *) "abcabcabcabc", 12,
                   0xE0u | (unsigned) t, v2[t], "V2");
        mppc_free(e);

        e = mppc_new(t);
        for (i = 0; i < 70; i++) {
            v3[i] = (unsigned char) i;
        }
        v3[70] = 0; v3[71] = 1; v3[72] = 2; v3[73] = 3;
        hex[0] = 0;
        for (i = 0; i < 70; i++) {
            sprintf(hex + strlen(hex), "%02X ", i);
        }
        strcat(hex, v3t[t]);
        check_body(e, v3, 74, 0xE0u | (unsigned) t, hex, "V3");
        mppc_free(e);

        e = mppc_new(t);
        check_body(e, (const unsigned char *) "ABCDEFGHABCDEFGH", 16,
                   0xE0u | (unsigned) t, v4a[t], "V4a");
        check_body(e, (const unsigned char *) "EFGHABCD!", 9,
                   0x20u | (unsigned) t, v4b[t], "V4b");
        mppc_free(e);

        e = mppc_new(t);
        memset(in, 0, 40);
        check_body(e, in, 40, 0xE0u | (unsigned) t, v6[t], "V6");
        mppc_free(e);

        e = mppc_new(t);
        memset(in, 0xFF, 8);
        check_body(e, in, 8, 0xE0u | (unsigned) t, v7[t], "V7");
        mppc_free(e);

        hex_to("01 00 01 00 00 00 00 00 0F 00 03 00 10 00 04 00 08 00 00 00 "
               "40 00", body);
        memset(body + 22, 7, 64);
        e = mppc_new(t);
        check_body(e, body, 86, 0xE0u | (unsigned) t, pb[t], "P");
        mppc_free(e);
    }
}

static unsigned int pack(MPPC_ENC *e, const unsigned char *in,
                         unsigned int n)
{
    const unsigned char *out;
    unsigned int outlen;

    return mppc_compress(e, in, n, &out, &outlen);
}

static void test_sequences(void)
{
    static unsigned char buf[17000];
    MPPC_ENC *e;
    int t;

    e = mppc_new(0);
    fill_words(buf, 3000);
    CHECK_EQ(pack(e, buf, 3000), 0xE0);
    CHECK_EQ(e->pos, 3000);
    fill_words(buf, 3000);
    CHECK_EQ(pack(e, buf, 3000), 0x20);
    CHECK_EQ(e->pos, 6000);
    fill_random(buf, 200);
    CHECK_EQ(pack(e, buf, 200), 0);
    CHECK_EQ(e->pos, 6000);
    CHECK_EQ(e->expanded, 1);
    fill_words(buf, 3000);
    CHECK_EQ(pack(e, buf, 3000), 0xE0);
    CHECK_EQ(e->pos, 3000);
    CHECK_EQ(e->restarts, 1);
    fill_words(buf, 1000);
    CHECK_EQ(pack(e, buf, 1000), 0x20);
    CHECK_EQ(e->pos, 4000);
    mppc_free(e);

    for (t = 0; t <= 1; t++) {
        e = mppc_new(t);
        fill_words(buf, e->max_in + 1);
        CHECK_EQ(pack(e, buf, e->max_in + 1), 0);
        CHECK_EQ(e->raw_big, 1);
        CHECK_EQ(e->pending, 0xC0);
        CHECK_EQ(pack(e, buf, e->max_in), 0xE0u | (unsigned) t);
        CHECK_EQ(e->pos, e->max_in);
        CHECK_EQ(pack(e, buf, e->max_in), t ? 0x21u : 0xE0u);
        CHECK(e->pos <= e->hsize - 4);
        mppc_restart(e);
        CHECK_EQ(pack(e, buf, 100), 0xE0u | (unsigned) t);
        mppc_free(e);
    }
}

static void test_roundtrip(FILE *stream)
{
    static unsigned char buf[17000];
    static DEC dec;
    MPPC_ENC *e;
    const unsigned char *out;
    unsigned int outlen, flags, n, k, pk, start;
    unsigned char hdr[8];
    long got;
    int t;
    unsigned long bodies = 0, packed = 0, raw = 0, firsts = 0;

    g_seed = 4711;
    for (t = 0; t <= 1; t++) {
        for (k = 0; k < 20; k++) {
            e = mppc_new(t);
            dec_init(&dec, t);
            for (pk = 0; pk < 60; pk++) {
                n = 1 + rnd() % (t ? 9000u : 4000u);
                switch (rnd() % 7) {
                case 0:
                    fill_random(buf, n);
                    break;
                case 1:
                case 2:
                    fill_words(buf, n);
                    break;
                default:
                    fill_text(buf, n);
                    break;
                }
                if (pk % 17 == 16) {
                    mppc_restart(e);
                }
                outlen = 0;
                flags = mppc_compress(e, buf, n, &out, &outlen);
                bodies++;
                if (flags == 0) {
                    raw++;
                } else {
                    packed++;
                    if (flags & 0xC0) {
                        firsts++;
                    }
                    CHECK_EQ(flags & 0x3F, 0x20u | (unsigned) t);
                    CHECK(outlen < n);
                    got = dec_body(&dec, t, flags, out, outlen, &start);
                    if (got != (long) n ||
                        memcmp(dec.hist + start, buf, n) != 0) {
                        t_failures++;
                        printf("t_mppc: round trip type %d stream %u body %u "
                               "(%u bytes, flags 0x%02X): decoded %ld\n", t,
                               k, pk, n, flags, got);
                    }
                }
                if (stream != NULL) {
                    hdr[0] = (unsigned char) t;
                    hdr[1] = (unsigned char) flags;
                    hdr[2] = (unsigned char) n;
                    hdr[3] = (unsigned char) (n >> 8);
                    hdr[4] = (unsigned char) (flags ? outlen : 0);
                    hdr[5] = (unsigned char) ((flags ? outlen : 0) >> 8);
                    hdr[6] = (unsigned char) k;
                    hdr[7] = (unsigned char) pk;
                    fwrite(hdr, 1, 8, stream);
                    fwrite(buf, 1, n, stream);
                    if (flags) {
                        fwrite(out, 1, outlen, stream);
                    }
                }
            }
            mppc_free(e);
        }
    }
    printf("t_mppc: round trip: %lu bodies, %lu compressed (%lu with 0xC0), "
           "%lu raw\n", bodies, packed, firsts, raw);
    CHECK(packed > 0 && raw > 0 && firsts > 0);
}

int main(int argc, char **argv)
{
    FILE *stream = NULL;

    if (argc == 3 && strcmp(argv[1], "-stream") == 0) {
        stream = fopen(argv[2], "wb");
        if (stream == NULL) {
            printf("t_mppc: cannot write %s\n", argv[2]);
            return 2;
        }
    }
    test_codes();
    test_vectors();
    test_sequences();
    test_roundtrip(stream);
    if (stream != NULL) {
        fclose(stream);
    }
    return t_result("t_mppc");
}
