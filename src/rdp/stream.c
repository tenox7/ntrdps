/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <string.h>
#include "stream.h"

void out_init(OUTS *s, unsigned char *buf, unsigned int cap)
{
    s->data = buf;
    s->cap = cap;
    s->len = 0;
    s->fail = 0;
}

static int out_fits(OUTS *s, unsigned int n)
{
    if (s->fail || n > s->cap - s->len) {
        s->fail = 1;
        return 0;
    }
    return 1;
}

void out_u8(OUTS *s, unsigned int v)
{
    if (out_fits(s, 1)) {
        s->data[s->len++] = (unsigned char) v;
    }
}

void out_u16le(OUTS *s, unsigned int v)
{
    if (out_fits(s, 2)) {
        s->data[s->len++] = (unsigned char) v;
        s->data[s->len++] = (unsigned char) (v >> 8);
    }
}

void out_u16be(OUTS *s, unsigned int v)
{
    if (out_fits(s, 2)) {
        s->data[s->len++] = (unsigned char) (v >> 8);
        s->data[s->len++] = (unsigned char) v;
    }
}

void out_u32le(OUTS *s, unsigned long v)
{
    if (out_fits(s, 4)) {
        s->data[s->len++] = (unsigned char) v;
        s->data[s->len++] = (unsigned char) (v >> 8);
        s->data[s->len++] = (unsigned char) (v >> 16);
        s->data[s->len++] = (unsigned char) (v >> 24);
    }
}

void out_u32be(OUTS *s, unsigned long v)
{
    if (out_fits(s, 4)) {
        s->data[s->len++] = (unsigned char) (v >> 24);
        s->data[s->len++] = (unsigned char) (v >> 16);
        s->data[s->len++] = (unsigned char) (v >> 8);
        s->data[s->len++] = (unsigned char) v;
    }
}

void out_bytes(OUTS *s, const void *p, unsigned int n)
{
    if (out_fits(s, n)) {
        memcpy(s->data + s->len, p, n);
        s->len += n;
    }
}

void out_zero(OUTS *s, unsigned int n)
{
    if (out_fits(s, n)) {
        memset(s->data + s->len, 0, n);
        s->len += n;
    }
}

unsigned int out_reserve(OUTS *s, unsigned int n)
{
    unsigned int at = s->len;

    out_zero(s, n);
    return at;
}

static int out_written(OUTS *s, unsigned int at, unsigned int n)
{
    if (s->fail || at > s->len || n > s->len - at) {
        s->fail = 1;
        return 0;
    }
    return 1;
}

void out_put_u8(OUTS *s, unsigned int at, unsigned int v)
{
    if (out_written(s, at, 1)) {
        s->data[at] = (unsigned char) v;
    }
}

void out_put_u16le(OUTS *s, unsigned int at, unsigned int v)
{
    if (out_written(s, at, 2)) {
        s->data[at] = (unsigned char) v;
        s->data[at + 1] = (unsigned char) (v >> 8);
    }
}

void out_put_u16be(OUTS *s, unsigned int at, unsigned int v)
{
    if (out_written(s, at, 2)) {
        s->data[at] = (unsigned char) (v >> 8);
        s->data[at + 1] = (unsigned char) v;
    }
}

void out_put_u32le(OUTS *s, unsigned int at, unsigned long v)
{
    if (out_written(s, at, 4)) {
        s->data[at] = (unsigned char) v;
        s->data[at + 1] = (unsigned char) (v >> 8);
        s->data[at + 2] = (unsigned char) (v >> 16);
        s->data[at + 3] = (unsigned char) (v >> 24);
    }
}

unsigned int out_len(const OUTS *s)
{
    return s->len;
}

unsigned int out_room(const OUTS *s)
{
    return s->fail ? 0 : s->cap - s->len;
}

void in_init(INS *s, const unsigned char *buf, unsigned int len)
{
    s->data = buf;
    s->len = len;
    s->pos = 0;
    s->fail = 0;
}

int in_need(INS *s, unsigned int n)
{
    if (s->fail || n > s->len - s->pos) {
        s->fail = 1;
        s->pos = s->len;
        return 0;
    }
    return 1;
}

unsigned int in_u8(INS *s)
{
    if (!in_need(s, 1)) {
        return 0;
    }
    return s->data[s->pos++];
}

unsigned int in_u16le(INS *s)
{
    unsigned int v;

    if (!in_need(s, 2)) {
        return 0;
    }
    v = s->data[s->pos] | ((unsigned int) s->data[s->pos + 1] << 8);
    s->pos += 2;
    return v;
}

unsigned int in_u16be(INS *s)
{
    unsigned int v;

    if (!in_need(s, 2)) {
        return 0;
    }
    v = ((unsigned int) s->data[s->pos] << 8) | s->data[s->pos + 1];
    s->pos += 2;
    return v;
}

unsigned long in_u32le(INS *s)
{
    unsigned long v;

    if (!in_need(s, 4)) {
        return 0;
    }
    v = (unsigned long) s->data[s->pos]
        | ((unsigned long) s->data[s->pos + 1] << 8)
        | ((unsigned long) s->data[s->pos + 2] << 16)
        | ((unsigned long) s->data[s->pos + 3] << 24);
    s->pos += 4;
    return v;
}

unsigned long in_u32be(INS *s)
{
    unsigned long v;

    if (!in_need(s, 4)) {
        return 0;
    }
    v = ((unsigned long) s->data[s->pos] << 24)
        | ((unsigned long) s->data[s->pos + 1] << 16)
        | ((unsigned long) s->data[s->pos + 2] << 8)
        | (unsigned long) s->data[s->pos + 3];
    s->pos += 4;
    return v;
}

void in_bytes(INS *s, void *dst, unsigned int n)
{
    if (!in_need(s, n)) {
        memset(dst, 0, n);
        return;
    }
    memcpy(dst, s->data + s->pos, n);
    s->pos += n;
}

void in_skip(INS *s, unsigned int n)
{
    if (in_need(s, n)) {
        s->pos += n;
    }
}

void in_sub(INS *s, INS *sub, unsigned int n)
{
    if (!in_need(s, n)) {
        in_init(sub, s->data + s->len, 0);
        sub->fail = 1;
        return;
    }
    in_init(sub, s->data + s->pos, n);
    s->pos += n;
}

unsigned int in_left(const INS *s)
{
    return s->len - s->pos;
}

const unsigned char *in_ptr(const INS *s)
{
    return s->data + s->pos;
}
