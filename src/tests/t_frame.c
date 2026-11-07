/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include "frame.h"
#include "tcheck.h"

static int fl(const char *bytes, unsigned int n, unsigned int *len)
{
    return frame_length((const unsigned char *) bytes, n, len);
}

static void test_tpkt(void)
{
    unsigned int len;

    CHECK_EQ(fl("", 0, &len), FRAME_MORE);
    CHECK_EQ(fl("\x03", 1, &len), FRAME_MORE);
    CHECK_EQ(fl("\x03\x00\x00", 3, &len), FRAME_MORE);
    CHECK_EQ(len, 0);

    CHECK_EQ(fl("\x03\x00\x00\x0b\x06\xe0", 6, &len), FRAME_TPKT);
    CHECK_EQ(len, 11);
    CHECK_EQ(fl("\x03\x00\x00\x07", 4, &len), FRAME_TPKT);
    CHECK_EQ(len, 7);
    CHECK_EQ(fl("\x03\x00\xff\xff", 4, &len), FRAME_TPKT);
    CHECK_EQ(len, 65535);
    CHECK_EQ(fl("\x03\x00\x01\x6e", 4, &len), FRAME_TPKT);
    CHECK_EQ(len, 366);

    CHECK_EQ(fl("\x03\x01\x00\x0b", 4, &len), FRAME_TPKT);
    CHECK_EQ(len, 11);

    CHECK_EQ(fl("\x03\x00\x00\x06", 4, &len), FRAME_ERROR);
    CHECK_EQ(len, 0);
    CHECK_EQ(fl("\x03\x00\x00\x00", 4, &len), FRAME_ERROR);
}

static void test_fastpath(void)
{
    unsigned int len;

    CHECK_EQ(fl("\x08\x06\x00\x1e\x01\x1e", 6, &len), FRAME_FASTPATH);
    CHECK_EQ(len, 6);
    CHECK_EQ(fl("\x08", 1, &len), FRAME_MORE);

    CHECK_EQ(fl("\x08\x81", 2, &len), FRAME_MORE);
    CHECK_EQ(fl("\x08\x81\x2c", 3, &len), FRAME_FASTPATH);
    CHECK_EQ(len, 300);
    CHECK_EQ(fl("\x00\xff\xff", 3, &len), FRAME_FASTPATH);
    CHECK_EQ(len, 32767);

    CHECK_EQ(fl("\xc4\x05", 2, &len), FRAME_FASTPATH);
    CHECK_EQ(len, 5);

    CHECK_EQ(fl("\x08\x01", 2, &len), FRAME_ERROR);
    CHECK_EQ(fl("\x08\x00", 2, &len), FRAME_ERROR);
    CHECK_EQ(fl("\x08\x80\x02", 3, &len), FRAME_ERROR);
    CHECK_EQ(fl("\x08\x80\x03", 3, &len), FRAME_FASTPATH);
    CHECK_EQ(len, 3);
}

static void test_other(void)
{
    unsigned int len;

    CHECK_EQ(fl("\x01", 1, &len), FRAME_ERROR);
    CHECK_EQ(fl("\x02", 1, &len), FRAME_ERROR);
    CHECK_EQ(fl("\x07\x00\x00\x0b", 4, &len), FRAME_ERROR);
    CHECK_EQ(fl("\x83\x00\x00\x0b", 4, &len), FRAME_ERROR);
    CHECK_EQ(fl("\xff", 1, &len), FRAME_ERROR);
    CHECK_EQ(fl("GET ", 4, &len), FRAME_ERROR);
    CHECK_EQ(len, 0);
}

int main(void)
{
    test_tpkt();
    test_fastpath();
    test_other();
    return t_result("t_frame");
}
