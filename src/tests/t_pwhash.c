/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include "pwhash.h"
#include "tcheck.h"

static const unsigned char rfc7914_1[64] = {
    0x55, 0xac, 0x04, 0x6e, 0x56, 0xe3, 0x08, 0x9f, 0xec, 0x16, 0x91, 0xc2,
    0x25, 0x44, 0xb6, 0x05, 0xf9, 0x41, 0x85, 0x21, 0x6d, 0xde, 0x04, 0x65,
    0xe6, 0x8b, 0x9d, 0x57, 0xc2, 0x0d, 0xac, 0xbc, 0x49, 0xca, 0x9c, 0xcc,
    0xf1, 0x79, 0xb6, 0x45, 0x99, 0x16, 0x64, 0xb3, 0x9d, 0x77, 0xef, 0x31,
    0x7c, 0x71, 0xb8, 0x45, 0xb1, 0xe3, 0x0b, 0xd5, 0x09, 0x11, 0x20, 0x41,
    0xd3, 0xa1, 0x97, 0x83
};

static const WCHAR pw[] = { 'P', 0xE4, 's', 's', 'w', 'o', 'r', 't', '!',
                            0 };
static const char stored[] =
    "pbkdf2-sha256:5000:000102030405060708090a0b0c0d0e0f:"
    "f04de0177a5d5ad7280efc13539318dcb002ed7b8775a7df0e85ba716695179f";

static void test_pbkdf2(void)
{
    unsigned char out[64];

    pwhash_pbkdf2((const unsigned char *) "passwd", 6,
                  (const unsigned char *) "salt", 4, 1, out, sizeof(out));
    CHECK_MEM(out, rfc7914_1, sizeof(out));
}

static void test_stored(void)
{
    static const WCHAR wrong[] = { 'P', 'a', 's', 's', 'w', 'o', 'r', 't',
                                   '!', 0 };
    unsigned char salt[PWHASH_SALT];
    char text[PWHASH_TEXT], bad[PWHASH_TEXT];
    unsigned int i;

    for (i = 0; i < PWHASH_SALT; i++) {
        salt[i] = (unsigned char) i;
    }
    pwhash_make(pw, 9, salt, text);
    CHECK(strcmp(text, stored) == 0);
    CHECK_EQ(pwhash_check(pw, 9, stored), 1);
    CHECK_EQ(pwhash_check(wrong, 9, stored), 0);
    CHECK_EQ(pwhash_check(pw, 8, stored), 0);
    strcpy(bad, stored);
    bad[strlen(bad) - 1] = 'e';
    CHECK_EQ(pwhash_check(pw, 9, bad), 0);
    strcpy(bad, stored);
    bad[strlen(bad) - 1] = '\0';
    CHECK_EQ(pwhash_check(pw, 9, bad), -1);
    CHECK_EQ(pwhash_check(pw, 9, "sha1:5000:00:00"), -1);
    CHECK_EQ(pwhash_check(pw, 9, "pbkdf2-sha256:0:"), -1);
    CHECK_EQ(pwhash_check(pw, 9, ""), -1);
}

static void test_salt(void)
{
    unsigned char a[PWHASH_SALT], b[PWHASH_SALT], zero[PWHASH_SALT];

    memset(zero, 0, sizeof(zero));
    pwhash_salt(a);
    Sleep(30);
    pwhash_salt(b);
    CHECK(memcmp(a, zero, sizeof(a)) != 0);
    CHECK(memcmp(a, b, sizeof(a)) != 0);
}

int main(void)
{
    test_pbkdf2();
    test_stored();
    test_salt();
    return t_result("t_pwhash");
}
