/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_PWHASH_H
#define NTRDPS_PWHASH_H

#include <windows.h>

#define PWHASH_ITER 5000
#define PWHASH_SALT 16
#define PWHASH_TEXT 128

void pwhash_pbkdf2(const unsigned char *pw, unsigned int pwlen,
                   const unsigned char *salt, unsigned int saltlen,
                   unsigned long iter, unsigned char *out,
                   unsigned int outlen);

void pwhash_salt(unsigned char salt[PWHASH_SALT]);

void pwhash_make(const WCHAR *pw, unsigned int n,
                 const unsigned char salt[PWHASH_SALT], char *out);

int pwhash_check(const WCHAR *pw, unsigned int n, const char *stored);

#endif
