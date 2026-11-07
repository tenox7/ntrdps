/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include "stream.h"
#include "tcheck.h"

static const unsigned char all_kinds[] = {
    0x01,
    0x34, 0x12,
    0x12, 0x34,
    0x78, 0x56, 0x34, 0x12,
    0x12, 0x34, 0x56, 0x78,
    'a', 'b', 'c',
    0x00, 0x00
};

static void test_writer(void)
{
    unsigned char buf[32];
    OUTS s;

    out_init(&s, buf, sizeof(buf));
    out_u8(&s, 1);
    out_u16le(&s, 0x1234);
    out_u16be(&s, 0x1234);
    out_u32le(&s, 0x12345678UL);
    out_u32be(&s, 0x12345678UL);
    out_bytes(&s, "abc", 3);
    out_zero(&s, 2);
    CHECK(!s.fail);
    CHECK_EQ(out_len(&s), sizeof(all_kinds));
    CHECK_MEM(buf, all_kinds, sizeof(all_kinds));
    CHECK_EQ(out_room(&s), sizeof(buf) - sizeof(all_kinds));
}

static void test_writer_cuts(void)
{
    static const unsigned char expect[] = { 0xFF, 0x45, 0x23, 0x23, 0x45 };
    unsigned char buf[8];
    OUTS s;

    out_init(&s, buf, sizeof(buf));
    out_u8(&s, 0x1FF);
    out_u16le(&s, 0x12345);
    out_u16be(&s, 0x12345);
    CHECK(!s.fail);
    CHECK_EQ(out_len(&s), sizeof(expect));
    CHECK_MEM(buf, expect, sizeof(expect));
}

static void test_reserve(void)
{
    static const unsigned char expect[] = {
        0x03, 0x00, 0x00, 0x0C,
        0x0C, 0x00,
        0xAA,
        0x0C, 0x00, 0x00, 0x00,
        'x'
    };
    unsigned char buf[16];
    unsigned int len_be, len_le, one, len32;
    OUTS s;

    out_init(&s, buf, sizeof(buf));
    out_u8(&s, 3);
    out_u8(&s, 0);
    len_be = out_reserve(&s, 2);
    len_le = out_reserve(&s, 2);
    one = out_reserve(&s, 1);
    len32 = out_reserve(&s, 4);
    out_u8(&s, 'x');
    CHECK_EQ(len_be, 2);
    CHECK_EQ(len_le, 4);
    CHECK_EQ(one, 6);
    CHECK_EQ(len32, 7);
    out_put_u16be(&s, len_be, out_len(&s));
    out_put_u16le(&s, len_le, out_len(&s));
    out_put_u8(&s, one, 0xAA);
    out_put_u32le(&s, len32, out_len(&s));
    CHECK(!s.fail);
    CHECK_EQ(out_len(&s), sizeof(expect));
    CHECK_MEM(buf, expect, sizeof(expect));

    out_put_u16le(&s, out_len(&s) - 1, 0x1234);
    CHECK(s.fail);
    CHECK_EQ(buf[sizeof(expect) - 1], 'x');
}

static void test_writer_overrun(void)
{
    unsigned char buf[8];
    OUTS s;
    int i;

    memset(buf, 0xEE, sizeof(buf));
    out_init(&s, buf, 4);
    out_u16le(&s, 0x0201);
    out_u8(&s, 3);
    out_u16be(&s, 0x0405);
    CHECK(s.fail);
    CHECK_EQ(out_len(&s), 3);
    CHECK_EQ(out_room(&s), 0);
    out_u8(&s, 6);
    CHECK_EQ(out_len(&s), 3);
    CHECK_EQ(buf[3], 0xEE);
    for (i = 4; i < 8; i++) {
        CHECK_EQ(buf[i], 0xEE);
    }
    CHECK_EQ(out_reserve(&s, 2), 3);
    CHECK_EQ(out_len(&s), 3);

    out_init(&s, buf, 4);
    out_bytes(&s, "12345", 5);
    CHECK(s.fail);
    CHECK_EQ(out_len(&s), 0);
    CHECK_EQ(buf[0], 0x01);

    out_init(&s, buf, 4);
    out_u32be(&s, 0x01020304UL);
    CHECK(!s.fail);
    out_u32le(&s, 1);
    CHECK(s.fail);
    CHECK_EQ(out_len(&s), 4);
}

static void test_reader(void)
{
    unsigned char abc[3];
    INS s;

    in_init(&s, all_kinds, sizeof(all_kinds));
    CHECK_EQ(in_left(&s), sizeof(all_kinds));
    CHECK_EQ(in_u8(&s), 1);
    CHECK_EQ(in_u16le(&s), 0x1234);
    CHECK_EQ(in_u16be(&s), 0x1234);
    CHECK_EQ(in_u32le(&s), 0x12345678UL);
    CHECK_EQ(in_u32be(&s), 0x12345678UL);
    CHECK(in_ptr(&s) == all_kinds + 13);
    in_bytes(&s, abc, 3);
    CHECK_MEM(abc, "abc", 3);
    CHECK_EQ(in_left(&s), 2);
    CHECK(in_need(&s, 2));
    in_skip(&s, 2);
    CHECK_EQ(in_left(&s), 0);
    CHECK(!s.fail);
}

static void test_reader_high_bits(void)
{
    static const unsigned char ff[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00 };
    INS s;

    in_init(&s, ff, sizeof(ff));
    CHECK_EQ(in_u32le(&s), 0xFFFFFFFFUL);
    CHECK_EQ(in_u16be(&s), 0x8000);
    in_init(&s, ff, sizeof(ff));
    CHECK_EQ(in_u32be(&s), 0xFFFFFFFFUL);
    CHECK_EQ(in_u16le(&s), 0x0080);
    CHECK(!s.fail);
}

static void test_reader_overrun(void)
{
    static const unsigned char three[] = { 0x01, 0x02, 0x03 };
    unsigned char dst[4];
    INS s;

    in_init(&s, three, sizeof(three));
    CHECK_EQ(in_u16le(&s), 0x0201);
    CHECK_EQ(in_u16le(&s), 0);
    CHECK(s.fail);
    CHECK_EQ(in_left(&s), 0);
    CHECK_EQ(in_u8(&s), 0);
    memset(dst, 0xEE, sizeof(dst));
    in_bytes(&s, dst, sizeof(dst));
    CHECK_EQ(dst[0], 0);
    CHECK_EQ(dst[3], 0);

    in_init(&s, three, sizeof(three));
    CHECK_EQ(in_u32be(&s), 0);
    CHECK(s.fail);

    in_init(&s, three, sizeof(three));
    CHECK(!in_need(&s, 4));
    CHECK(s.fail);
    CHECK_EQ(in_u8(&s), 0);

    in_init(&s, three, sizeof(three));
    in_skip(&s, 4);
    CHECK(s.fail);

    in_init(&s, three, 0);
    CHECK_EQ(in_u8(&s), 0);
    CHECK(s.fail);
}

static void test_sub(void)
{
    static const unsigned char data[] = { 0x03, 0x0A, 0x0B, 0x0C, 0x09 };
    INS s, sub;

    in_init(&s, data, sizeof(data));
    in_sub(&s, &sub, in_u8(&s));
    CHECK_EQ(in_left(&sub), 3);
    CHECK_EQ(in_u8(&sub), 0x0A);
    CHECK_EQ(in_u16be(&sub), 0x0B0C);
    CHECK_EQ(in_u8(&sub), 0);
    CHECK(sub.fail);
    CHECK(!s.fail);
    CHECK_EQ(in_u8(&s), 0x09);
    CHECK(!s.fail);

    in_init(&s, data, sizeof(data));
    in_skip(&s, 1);
    in_sub(&s, &sub, 5);
    CHECK(s.fail);
    CHECK(sub.fail);
    CHECK_EQ(in_left(&sub), 0);
    CHECK_EQ(in_u8(&sub), 0);
}

int main(void)
{
    test_writer();
    test_writer_cuts();
    test_reserve();
    test_writer_overrun();
    test_reader();
    test_reader_high_bits();
    test_reader_overrun();
    test_sub();
    return t_result("t_stream");
}
