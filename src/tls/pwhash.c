/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "bearssl.h"
#include "pwhash.h"

#define PREFIX "pbkdf2-sha256:"
#define PW_MAX 256

void pwhash_pbkdf2(const unsigned char *pw, unsigned int pwlen,
                   const unsigned char *salt, unsigned int saltlen,
                   unsigned long iter, unsigned char *out,
                   unsigned int outlen)
{
    br_hmac_key_context kc;
    br_hmac_context hc;
    unsigned char u[32], t[32], num[4];
    unsigned long block = 1, j;
    unsigned int i, n;

    br_hmac_key_init(&kc, &br_sha256_vtable, pw, (size_t) pwlen);
    while (outlen > 0) {
        num[0] = (unsigned char) (block >> 24);
        num[1] = (unsigned char) (block >> 16);
        num[2] = (unsigned char) (block >> 8);
        num[3] = (unsigned char) block;
        br_hmac_init(&hc, &kc, 0);
        br_hmac_update(&hc, salt, (size_t) saltlen);
        br_hmac_update(&hc, num, 4);
        br_hmac_out(&hc, u);
        memcpy(t, u, sizeof(t));
        for (j = 1; j < iter; j++) {
            br_hmac_init(&hc, &kc, 0);
            br_hmac_update(&hc, u, sizeof(u));
            br_hmac_out(&hc, u);
            for (i = 0; i < sizeof(t); i++) {
                t[i] ^= u[i];
            }
        }
        n = outlen < sizeof(t) ? outlen : (unsigned int) sizeof(t);
        memcpy(out, t, n);
        out += n;
        outlen -= n;
        block++;
    }
    memset(u, 0, sizeof(u));
    memset(t, 0, sizeof(t));
    memset(&kc, 0, sizeof(kc));
    memset(&hc, 0, sizeof(hc));
}

void pwhash_salt(unsigned char salt[PWHASH_SALT])
{
    br_sha256_context c;
    unsigned char h[32];
    struct {
        DWORD         tick, pid, tid;
        SYSTEMTIME    st;
        LARGE_INTEGER pc;
        MEMORYSTATUS  ms;
        void         *addr;
    } e;

    memset(&e, 0, sizeof(e));
    e.tick = GetTickCount();
    e.pid = GetCurrentProcessId();
    e.tid = GetCurrentThreadId();
    GetSystemTime(&e.st);
    QueryPerformanceCounter(&e.pc);
    e.ms.dwLength = sizeof(e.ms);
    GlobalMemoryStatus(&e.ms);
    e.addr = (void *) &e;
    br_sha256_init(&c);
    br_sha256_update(&c, &e, sizeof(e));
    br_sha256_out(&c, h);
    memcpy(salt, h, PWHASH_SALT);
}

static unsigned int le_bytes(const WCHAR *pw, unsigned int n,
                             unsigned char *b)
{
    unsigned int i;

    if (n > PW_MAX) {
        n = PW_MAX;
    }
    for (i = 0; i < n; i++) {
        b[2 * i] = (unsigned char) (pw[i] & 0xFF);
        b[2 * i + 1] = (unsigned char) ((pw[i] >> 8) & 0xFF);
    }
    return 2 * n;
}

static void to_hex(char *d, const unsigned char *s, unsigned int n)
{
    static const char hx[] = "0123456789abcdef";
    unsigned int i;

    for (i = 0; i < n; i++) {
        d[2 * i] = hx[s[i] >> 4];
        d[2 * i + 1] = hx[s[i] & 15];
    }
    d[2 * n] = '\0';
}

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int from_hex(unsigned char *d, const char *s, unsigned int n,
                    char stop)
{
    unsigned int i;
    int hi, lo;

    for (i = 0; i < n; i++) {
        hi = hex_digit(s[2 * i]);
        lo = hex_digit(hi < 0 ? 0 : s[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return 0;
        }
        d[i] = (unsigned char) (hi << 4 | lo);
    }
    return s[2 * n] == stop;
}

void pwhash_make(const WCHAR *pw, unsigned int n,
                 const unsigned char salt[PWHASH_SALT], char *out)
{
    unsigned char b[2 * PW_MAX], h[32];
    unsigned int len = le_bytes(pw, n, b);

    pwhash_pbkdf2(b, len, salt, PWHASH_SALT, PWHASH_ITER, h, sizeof(h));
    sprintf(out, PREFIX "%lu:", (unsigned long) PWHASH_ITER);
    to_hex(out + strlen(out), salt, PWHASH_SALT);
    strcat(out, ":");
    to_hex(out + strlen(out), h, sizeof(h));
    memset(b, 0, sizeof(b));
    memset(h, 0, sizeof(h));
}

int pwhash_check(const WCHAR *pw, unsigned int n, const char *stored)
{
    unsigned char b[2 * PW_MAX], salt[PWHASH_SALT], want[32], h[32];
    unsigned long iter = 0;
    unsigned int len, i, diff = 0;
    const char *p;

    if (strncmp(stored, PREFIX, strlen(PREFIX)) != 0) {
        return -1;
    }
    p = stored + strlen(PREFIX);
    for (i = 0; p[i] >= '0' && p[i] <= '9' && i < 8; i++) {
        iter = iter * 10 + (unsigned long) (p[i] - '0');
    }
    if (i == 0 || p[i] != ':' || iter < 1 || iter > 10000000UL) {
        return -1;
    }
    p += i + 1;
    if (!from_hex(salt, p, PWHASH_SALT, ':') ||
        !from_hex(want, p + 2 * PWHASH_SALT + 1, sizeof(want), '\0')) {
        return -1;
    }
    len = le_bytes(pw, n, b);
    pwhash_pbkdf2(b, len, salt, PWHASH_SALT, iter, h, sizeof(h));
    for (i = 0; i < sizeof(h); i++) {
        diff |= (unsigned int) (h[i] ^ want[i]);
    }
    memset(b, 0, sizeof(b));
    memset(h, 0, sizeof(h));
    return diff == 0 ? 1 : 0;
}
