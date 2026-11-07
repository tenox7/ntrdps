/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_MPPC_H
#define NTRDPS_MPPC_H

#define MPPC_FLUSHED    0x80
#define MPPC_AT_FRONT   0x40
#define MPPC_COMPRESSED 0x20

#define MPPC_MIN_BODY   33

typedef struct MPPC_ENC_s {
    int            type;
    unsigned int   hsize;
    unsigned int   max_in;
    unsigned int   pos;
    unsigned char  pending;
    unsigned char *hist;
    unsigned short *table;
    unsigned char *out;
    unsigned long  bodies;
    unsigned long  packed;
    unsigned long  raw_big;
    unsigned long  expanded;
    unsigned long  restarts;
    double         bytes_in;
    double         bytes_out;
    double         ms;
} MPPC_ENC;

MPPC_ENC *mppc_new(int type);
void      mppc_free(MPPC_ENC *e);

void mppc_restart(MPPC_ENC *e);

unsigned int mppc_compress(MPPC_ENC *e, const unsigned char *src,
                           unsigned int n, const unsigned char **out,
                           unsigned int *outlen);

int mppc_copy_bits(int type, unsigned int offset, unsigned int length,
                   unsigned char *out, unsigned int cap);

#endif
