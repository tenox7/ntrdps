/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <stdio.h>
#include <string.h>
#include "rle.h"
#include "rledec.h"
#include "tcheck.h"

#define MAX_PIX   4096
#define OUT_SIZE  32768

#define SEED           20261101UL
#define TILES_PER_KIND 100
#define MODEL_CRC      0x88883B60UL

static int bpp_bytes(int bpp)
{
    return bpp == 8 ? 1 : bpp == 24 ? 3 : 2;
}

static void to_wire(const unsigned long *v, long n, int bpp,
                    unsigned char *out)
{
    int b = bpp_bytes(bpp);
    int i;
    long p;

    for (p = 0; p < n; p++) {
        for (i = 0; i < b; i++) {
            *out++ = (unsigned char) (v[p] >> (8 * i) & 0xFF);
        }
    }
}

static void print_hex(const unsigned char *p, int n)
{
    int i;

    for (i = 0; i < n && i < 48; i++) {
        printf(" %02X", p[i]);
    }
    printf(n > 48 ? " ...\n" : "\n");
}

static void test_headers(void)
{
    static const struct {
        const char *bytes;
        long len;
        int kind;
        long n;
    } t[] = {
        { "\x1F", 1, RLEDEC_BG, 31 },
        { "\x00\x00", 2, RLEDEC_BG, 32 },
        { "\x00\xFF", 2, RLEDEC_BG, 287 },
        { "\xF0\x20\x01", 3, RLEDEC_BG, 288 },
        { "\xC0\x00", 2, RLEDEC_SETFG, 16 },
        { "\xC0\xFF", 2, RLEDEC_SETFG, 271 },
        { "\x40\x00", 2, RLEDEC_FGBG, 1 },
        { "\x40\xFF", 2, RLEDEC_FGBG, 256 },
        { "\x5F", 1, RLEDEC_FGBG, 248 },
        { "\x41", 1, RLEDEC_FGBG, 8 },
        { "\xD1", 1, RLEDEC_SETFGBG, 8 },
        { "\xDF", 1, RLEDEC_SETFGBG, 120 },
        { "\xD0\x77", 2, RLEDEC_SETFGBG, 120 },
        { "\x21", 1, RLEDEC_FG, 1 },
        { "\x7F", 1, RLEDEC_COLOR, 31 },
        { "\x80\x00", 2, RLEDEC_IMAGE, 32 },
        { "\xEF", 1, RLEDEC_DITHER, 15 },
        { "\xE0\x00", 2, RLEDEC_DITHER, 16 },
        { "\xF2\x00\x01", 3, RLEDEC_FGBG, 256 },
        { "\xF8\x2C\x01", 3, RLEDEC_DITHER, 300 },
        { "\xF9", 1, RLEDEC_SPECIAL_1, 8 },
        { "\xFA", 1, RLEDEC_SPECIAL_2, 8 },
        { "\xFD", 1, RLEDEC_WHITE, 1 },
        { "\xFE", 1, RLEDEC_BLACK, 1 }
    };
    static const unsigned char bad[] = {
        0xA0, 0xA1, 0xBF, 0xF5, 0xFB, 0xFC, 0xFF
    };
    unsigned char code[3];
    unsigned int k;
    long i, n;

    for (k = 0; k < sizeof(t) / sizeof(t[0]); k++) {
        i = 0;
        n = -1;
        CHECK_EQ(rledec_header((const unsigned char *) t[k].bytes,
                               t[k].len, &i, &n), t[k].kind);
        CHECK_EQ(n, t[k].n);
        CHECK_EQ(i, t[k].len);
    }
    for (k = 0; k < sizeof(bad); k++) {
        code[0] = bad[k];
        code[1] = 1;
        code[2] = 1;
        i = 0;
        CHECK_EQ(rledec_header(code, 3, &i, &n), RLEDEC_BAD);
    }
    i = 0;
    CHECK_EQ(rledec_header((const unsigned char *) "\x00", 1, &i, &n),
             RLEDEC_BAD);
    i = 0;
    CHECK_EQ(rledec_header((const unsigned char *) "\xF3\x01", 2, &i, &n),
             RLEDEC_BAD);
}

static void dec_vector(int line, int w, int h, const char *stream, long len,
                       const char *ms, const char *rd)
{
    static unsigned char got[MAX_PIX];
    long n = (long) w * h;

    if (!rledec((const unsigned char *) stream, len, w, h, 8, RLEDEC_MS, got)
        || memcmp(got, ms, n) != 0) {
        t_failures++;
        printf("t_rle.c(%d): decoder vector, mode ms:", line);
        print_hex(got, (int) n);
    }
    if (!rledec((const unsigned char *) stream, len, w, h, 8, RLEDEC_RD, got)
        || memcmp(got, rd, n) != 0) {
        t_failures++;
        printf("t_rle.c(%d): decoder vector, mode rd:", line);
        print_hex(got, (int) n);
    }
}

#define DEC_VECTOR(w, h, stream, ms, rd) \
    dec_vector(__LINE__, w, h, stream, sizeof(stream) - 1, ms, rd)

static void test_decoder(void)
{
    static unsigned char got[64];

    DEC_VECTOR(8, 4, "\x63\x11\x84\x01\x02\x03\x04\xFE\x02\x03\xC2\x0F\x21"
               "\x41\xA5\xF9",
               "\x11\x11\x11\x01\x02\x03\x04\x00"
               "\x11\x11\xEE\x01\x02\x0C\x0B\x0F"
               "\x1E\x11\xE1\x01\x02\x03\x0B\x00"
               "\x11\x1E\xE1\x01\x02\x03\x0B\x00",
               "\x11\x11\x11\x01\x02\x03\x04\x00"
               "\x11\x11\xEE\x01\x02\x0C\x0B\x0F"
               "\x1E\x11\xE1\x01\x02\x03\x0B\x00"
               "\x11\x1E\xE1\x01\x02\x03\x0B\x00");
    DEC_VECTOR(4, 2, "\x62\x05\x04\x82\x09\x09",
               "\x05\x05\x00\x00\x00\x00\x09\x09",
               "\x05\x05\x00\x00\x05\x05\x09\x09");
    DEC_VECTOR(4, 2, "\x62\x05\x03\x02\x81\x09",
               "\x05\x05\x00\x00\x00\x05\x00\x09",
               "\x05\x05\x00\x00\x05\xFA\x00\x09");
    CHECK(rledec((const unsigned char *) "\xFD\x83\x01\x00\x02\x00\x03\x00",
                 8, 4, 1, 15, RLEDEC_MS, got));
    CHECK_MEM(got, "\xFF\x7F\x01\x00\x02\x00\x03\x00", 8);
    CHECK(rledec((const unsigned char *) "\xFD\x83\x01\x00\x02\x00\x03\x00",
                 8, 4, 1, 15, RLEDEC_RD, got));
    CHECK_MEM(got, "\xFF\xFF\x01\x00\x02\x00\x03\x00", 8);
    CHECK(!rledec((const unsigned char *) "\x63\x11", 2, 4, 1, 8, RLEDEC_MS,
                  got));
    CHECK(!rledec((const unsigned char *) "\x65\x11", 2, 4, 1, 8, RLEDEC_MS,
                  got));
    CHECK(!rledec((const unsigned char *) "\x64\x11\xFE", 3, 4, 1, 8,
                  RLEDEC_MS, got));
    CHECK(!rledec((const unsigned char *) "\xA4\x11", 2, 4, 1, 8, RLEDEC_MS,
                  got));
}

static void enc_vector(int line, int bpp, int w, int h,
                       const unsigned long *v, const char *want, int len)
{
    static unsigned char pix[MAX_PIX * 3], out[OUT_SIZE], back[MAX_PIX * 3];
    long raw = (long) w * h * bpp_bytes(bpp);
    int got;

    to_wire(v, (long) w * h, bpp, pix);
    got = rle_encode(pix, w, h, bpp, out, sizeof(out));
    if (got != len || memcmp(out, want, len) != 0) {
        t_failures++;
        printf("t_rle.c(%d): encoder vector, %d bytes:", line, got);
        print_hex(out, got);
    }
    if (!rledec(out, got, w, h, bpp, RLEDEC_MS, back) ||
        memcmp(back, pix, raw) != 0 ||
        !rledec(out, got, w, h, bpp, RLEDEC_RD, back) ||
        memcmp(back, pix, raw) != 0) {
        t_failures++;
        printf("t_rle.c(%d): encoder vector does not decode back\n", line);
    }
}

#define ENC_VECTOR(bpp, w, h, v, want) \
    enc_vector(__LINE__, bpp, w, h, v, want, sizeof(want) - 1)

static void fill(unsigned long *v, long from, long n, unsigned long value)
{
    while (n-- > 0) {
        v[from++] = value;
    }
}

static void test_encoder_vectors(void)
{
    static const unsigned long t1[16] = {
        7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 0xFF, 7, 7, 7, 7
    };
    static const unsigned long t2[48] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };
    static const unsigned long t3[12] = {
        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
        0xF800, 0xF800, 0xF800, 0xF800,
        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    };
    static const unsigned long t4[16] = {
        0x008080, 0x008080, 0x008080, 0x008080,
        0x008080, 0x008080, 0x008080, 0x008080,
        0xC0C0C0, 0x808080, 0xC0C0C0, 0x808080,
        0xC0C0C0, 0x808080, 0xC0C0C0, 0x808080
    };
    static const unsigned long image8[8] = {
        0xFF, 0x12, 0x34, 0x00, 0x56, 0x78, 0x9A, 0xBC
    };
    static const unsigned long image15[4] = { 0x7FFF, 0x1234, 0, 0x2345 };
    static const unsigned long image24[4] = {
        0xFFFFFF, 0x123456, 0, 0x654321
    };
    static const unsigned long white8[8] = {
        0x11, 0x11, 0x11, 0xFF, 0x22, 0x22, 0x22, 0x22
    };
    static const unsigned long white15[8] = {
        0x1111, 0x1111, 0x1111, 0x7FFF, 0x2222, 0x2222, 0x2222, 0x2222
    };
    static const unsigned long white16[8] = {
        0x1111, 0x1111, 0x1111, 0xFFFF, 0x2222, 0x2222, 0x2222, 0x2222
    };
    static const unsigned long black8[16] = {
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
        0x55, 0x55, 0x55, 0x00, 0x66, 0x66, 0x66, 0x66
    };
    static const unsigned long fgbg8[8] = {
        0x11, 0x11, 0x11, 0x00, 0x22, 0x22, 0x22, 0x22
    };
    static const unsigned long setfgbg8[16] = {
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
        0x51, 0x51, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11
    };
    static const unsigned long special1[16] = {
        0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
        0x00, 0x00, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40
    };
    static const unsigned long special2[16] = {
        0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
        0x00, 0x40, 0x00, 0x40, 0x40, 0x40, 0x40, 0x40
    };
    static unsigned long v[600];
    long i;

    ENC_VECTOR(8, 8, 2, t1, "\x6B\x07\xD0\x04\xF8\x01");
    ENC_VECTOR(8, 8, 6, t2, "\x69\xFF\xD0\x26\xFF\x01\x02\x02\x06\x07");
    ENC_VECTOR(16, 4, 3, t3, "\xC4\xFF\xFF\xC8\xFF\x07");
    ENC_VECTOR(24, 8, 2, t4, "\xC8\x80\x80\x00\xE4\xC0\xC0\xC0\x80\x80\x80");

    ENC_VECTOR(8, 8, 1, image8, "\x88\xFF\x12\x34\x00\x56\x78\x9A\xBC");
    ENC_VECTOR(15, 4, 1, image15, "\x82\xFF\x7F\x34\x12\x01\x81\x45\x23");
    ENC_VECTOR(24, 4, 1, image24,
               "\x82\xFF\xFF\xFF\x56\x34\x12\x01\x81\x21\x43\x65");
    ENC_VECTOR(8, 8, 1, white8, "\xC3\x11\xFD\xC4\x22");
    ENC_VECTOR(15, 8, 1, white15, "\xC3\x11\x11\x81\xFF\x7F\xC4\x22\x22");
    ENC_VECTOR(16, 8, 1, white16, "\xC3\x11\x11\xFD\xC4\x22\x22");
    ENC_VECTOR(8, 8, 2, black8, "\xC8\x11\xC3\x44\xFE\xC4\x77");
    ENC_VECTOR(8, 8, 1, fgbg8, "\xC3\x11\xD0\x04\x22\x1E");
    ENC_VECTOR(8, 8, 2, setfgbg8, "\xC8\x11\xD1\x40\x03");
    ENC_VECTOR(8, 8, 2, special1, "\xC8\x40\xF9");
    ENC_VECTOR(8, 8, 2, special2, "\xC8\x40\xFA");

    for (i = 0; i < 600; i += 2) {
        v[i] = 0x11;
        v[i + 1] = 0x22;
    }
    ENC_VECTOR(8, 600, 1, v, "\xF8\x2C\x01\x11\x22");
    fill(v, 0, 320, 0);
    ENC_VECTOR(8, 4, 80, v, "\xF3\x40\x01\x00");
    fill(v, 0, 64, 0x1234);
    ENC_VECTOR(16, 64, 1, v, "\xC0\x30\x34\x12");
    fill(v, 0, 400, 0x42);
    ENC_VECTOR(8, 400, 1, v, "\xF6\x90\x01\x42");
}

static void test_limits(void)
{
    static unsigned char pix[16], out[64];
    int n;

    memset(pix, 0x42, sizeof(pix));
    CHECK_EQ(rle_encode(pix, 4, 1, 8, out, sizeof(out)), 2);
    CHECK_EQ(rle_encode(pix, 4, 1, 8, out, 2), 2);
    CHECK_EQ(rle_encode(pix, 4, 1, 8, out, 1), 0);
    CHECK_EQ(rle_encode(pix, 4, 1, 8, out, 0), 0);
    CHECK_EQ(rle_encode(pix, 4, 1, 8, out, -5), 0);
    CHECK_EQ(rle_encode(pix, 6, 1, 8, out, sizeof(out)), 0);
    CHECK_EQ(rle_encode(pix, 0, 1, 8, out, sizeof(out)), 0);
    CHECK_EQ(rle_encode(pix, 4, 0, 8, out, sizeof(out)), 0);
    CHECK_EQ(rle_encode(pix, 4, 1, 32, out, sizeof(out)), 0);
    CHECK_EQ(rle_encode(pix, 4, 16384, 8, out, sizeof(out)), 0);
    for (n = 0; n < 16; n++) {
        pix[n] = (unsigned char) (n * 17 + 1);
    }
    CHECK_EQ(rle_encode(pix, 16, 1, 8, out, 17), 17);
    CHECK_EQ(rle_encode(pix, 16, 1, 8, out, 16), 0);
}

static unsigned long lcg_x;

static unsigned long lcg_next(void)
{
    lcg_x = (lcg_x * 1103515245UL + 12345UL) & 0xFFFFFFFFUL;
    return lcg_x >> 8;
}

static long below(long n)
{
    return (long) (lcg_next() % (unsigned long) n);
}

static unsigned long max_value(int bpp)
{
    return bpp == 8 ? 0xFF : bpp == 15 ? 0x7FFF : bpp == 16 ? 0xFFFF
                                                            : 0xFFFFFF;
}

static void gen(unsigned long *v, long w, long h, int bpp, int kind)
{
    static unsigned long text[MAX_PIX];
    unsigned long pal[6], top = max_value(bpp), c, fg, bg;
    long n = w * h, i, x, y, x0, y0, x1, y1, rects;

    for (i = 0; i < 4; i++) {
        pal[i] = lcg_next() & top;
    }
    pal[4] = 0;
    pal[5] = top;
    switch (kind) {
    case 0:
        for (i = 0; i < n; i++) {
            v[i] = lcg_next() & top;
        }
        break;
    case 1:
        fill(v, 0, n, pal[below(6)]);
        rects = below(6);
        for (i = 0; i < rects; i++) {
            x0 = below(w);
            y0 = below(h);
            x1 = x0 + below(w - x0);
            y1 = y0 + below(h - y0);
            c = pal[below(6)];
            for (y = y0; y <= y1; y++) {
                fill(v, y * w + x0, x1 - x0 + 1, c);
            }
        }
        break;
    case 2:
        bg = pal[below(6)];
        fg = pal[below(6)];
        for (i = 0; i < n; i++) {
            v[i] = below(4) == 0 ? fg : bg;
        }
        break;
    case 3:
        fg = pal[below(6)];
        bg = pal[below(6)];
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                v[y * w + x] = ((x + y) & 1) == 0 ? fg : bg;
            }
        }
        break;
    default:
        gen(v, w, h, bpp, 1);
        gen(text, w, h, bpp, 2);
        for (i = 0; i < n; i++) {
            if (below(10) < 3) {
                v[i] = text[i];
            }
        }
        break;
    }
}

static unsigned long crc_add(unsigned long crc, const unsigned char *p,
                             long n)
{
    int k;

    crc ^= 0xFFFFFFFFUL;
    while (n-- > 0) {
        crc ^= *p++;
        for (k = 0; k < 8; k++) {
            crc = crc & 1 ? (crc >> 1) ^ 0xEDB88320UL : crc >> 1;
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

static void dump_tile(FILE *f, int bpp, int w, int h, const unsigned char *pix,
                      const unsigned char *rle, int len)
{
    unsigned char head[7];

    head[0] = (unsigned char) bpp;
    head[1] = (unsigned char) (w & 0xFF);
    head[2] = (unsigned char) (w >> 8);
    head[3] = (unsigned char) (h & 0xFF);
    head[4] = (unsigned char) (h >> 8);
    head[5] = (unsigned char) (len & 0xFF);
    head[6] = (unsigned char) (len >> 8);
    fwrite(head, 1, sizeof(head), f);
    fwrite(pix, 1, (size_t) w * h * bpp_bytes(bpp), f);
    fwrite(rle, 1, len, f);
}

static void test_random_tiles(FILE *dump)
{
    static const int depths[4] = { 8, 15, 16, 24 };
    static unsigned long v[MAX_PIX];
    static unsigned char pix[MAX_PIX * 3], out[OUT_SIZE], back[MAX_PIX * 3];
    static unsigned char again[OUT_SIZE];
    unsigned char two[2];
    unsigned long crc = 0;
    long raw, raw_all = 0, rle_all = 0;
    int d, kind, k, w, h, len, tiles = 0, bad = 0;

    lcg_x = SEED;
    for (d = 0; d < 4; d++) {
        for (kind = 0; kind < 5; kind++) {
            for (k = 0; k < TILES_PER_KIND; k++) {
                w = 4 * (1 + (int) below(16));
                h = 1 + (int) below(64);
                gen(v, w, h, depths[d], kind);
                to_wire(v, (long) w * h, depths[d], pix);
                raw = (long) w * h * bpp_bytes(depths[d]);
                len = rle_encode(pix, w, h, depths[d], out, sizeof(out));
                if (len <= 0 ||
                    !rledec(out, len, w, h, depths[d], RLEDEC_MS, back) ||
                    memcmp(back, pix, raw) != 0 ||
                    !rledec(out, len, w, h, depths[d], RLEDEC_RD, back) ||
                    memcmp(back, pix, raw) != 0 ||
                    rle_encode(pix, w, h, depths[d], again, len) != len ||
                    rle_encode(pix, w, h, depths[d], again, len - 1) != 0) {
                    t_failures++;
                    if (++bad <= 5) {
                        printf("t_rle: tile %d (%d bpp, kind %d, %d x %d): "
                               "%d RLE bytes; no exact round trip or max "
                               "check\n", tiles, depths[d], kind, w, h, len);
                    }
                }
                two[0] = (unsigned char) (len & 0xFF);
                two[1] = (unsigned char) (len >> 8);
                crc = crc_add(crc, two, 2);
                crc = crc_add(crc, out, len);
                if (dump != NULL) {
                    dump_tile(dump, depths[d], w, h, pix, out, len);
                }
                tiles++;
                raw_all += raw;
                rle_all += len;
            }
        }
    }
    printf("t_rle: %d random tiles, raw %ld bytes -> RLE %ld bytes, "
           "CRC-32 0x%08lX (model: 0x%08lX)\n", tiles, raw_all, rle_all,
           crc, MODEL_CRC);
    CHECK_EQ(crc, MODEL_CRC);
}

int main(int argc, char **argv)
{
    FILE *dump = NULL;

    if (argc == 3 && strcmp(argv[1], "-dump") == 0) {
        dump = fopen(argv[2], "wb");
        if (dump == NULL) {
            printf("t_rle: cannot write %s\n", argv[2]);
            return 2;
        }
    }
    test_headers();
    test_decoder();
    test_encoder_vectors();
    test_limits();
    test_random_tiles(dump);
    if (dump != NULL) {
        fclose(dump);
    }
    return t_result("t_rle");
}
