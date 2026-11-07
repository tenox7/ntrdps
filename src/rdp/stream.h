/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_STREAM_H
#define NTRDPS_STREAM_H

typedef struct {
    unsigned char *data;
    unsigned int   cap;
    unsigned int   len;
    int            fail;
} OUTS;

typedef struct {
    const unsigned char *data;
    unsigned int   len;
    unsigned int   pos;
    int            fail;
} INS;

void out_init(OUTS *s, unsigned char *buf, unsigned int cap);
void out_u8(OUTS *s, unsigned int v);
void out_u16le(OUTS *s, unsigned int v);
void out_u16be(OUTS *s, unsigned int v);
void out_u32le(OUTS *s, unsigned long v);
void out_u32be(OUTS *s, unsigned long v);
void out_bytes(OUTS *s, const void *p, unsigned int n);
void out_zero(OUTS *s, unsigned int n);

unsigned int out_reserve(OUTS *s, unsigned int n);
void out_put_u8(OUTS *s, unsigned int at, unsigned int v);
void out_put_u16le(OUTS *s, unsigned int at, unsigned int v);
void out_put_u16be(OUTS *s, unsigned int at, unsigned int v);
void out_put_u32le(OUTS *s, unsigned int at, unsigned long v);

unsigned int out_len(const OUTS *s);
unsigned int out_room(const OUTS *s);

void in_init(INS *s, const unsigned char *buf, unsigned int len);
unsigned int  in_u8(INS *s);
unsigned int  in_u16le(INS *s);
unsigned int  in_u16be(INS *s);
unsigned long in_u32le(INS *s);
unsigned long in_u32be(INS *s);
void in_bytes(INS *s, void *dst, unsigned int n);
void in_skip(INS *s, unsigned int n);

int in_need(INS *s, unsigned int n);

void in_sub(INS *s, INS *sub, unsigned int n);

unsigned int in_left(const INS *s);
const unsigned char *in_ptr(const INS *s);

#endif
