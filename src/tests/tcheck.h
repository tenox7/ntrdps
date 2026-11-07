/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_TCHECK_H
#define NTRDPS_TCHECK_H

#include <stdio.h>
#include <string.h>

static int t_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            t_failures++; \
            printf("%s(%d): check failed: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define CHECK_EQ(a, b) \
    do { \
        unsigned long t_a_ = (unsigned long) (a); \
        unsigned long t_b_ = (unsigned long) (b); \
        if (t_a_ != t_b_) { \
            t_failures++; \
            printf("%s(%d): check failed: %s == %s (0x%lx != 0x%lx)\n", \
                   __FILE__, __LINE__, #a, #b, t_a_, t_b_); \
        } \
    } while (0)

#define CHECK_MEM(a, b, n) \
    do { \
        if (memcmp((a), (b), (n)) != 0) { \
            t_failures++; \
            printf("%s(%d): check failed: bytes of %s differ from %s\n", \
                   __FILE__, __LINE__, #a, #b); \
        } \
    } while (0)

static int t_result(const char *name)
{
    printf("%s: %s (%d failures)\n", name, t_failures ? "FAIL" : "PASS",
           t_failures);
    return t_failures ? 1 : 0;
}

#endif
