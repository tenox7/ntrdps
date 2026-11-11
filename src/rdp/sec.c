/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <string.h>
#include "log.h"
#include "stream.h"
#include "session.h"
#include "mcs.h"
#include "gcc.h"
#include "sec.h"
#include "share.h"

static const struct {
    unsigned long bit;
    const char   *name;
} info_names[] = {
    { 0x00000001UL, "MOUSE" },
    { 0x00000002UL, "DISABLECTRLALTDEL" },
    { 0x00000008UL, "AUTOLOGON" },
    { 0x00000010UL, "UNICODE" },
    { 0x00000020UL, "MAXIMIZESHELL" },
    { 0x00000040UL, "LOGONNOTIFY" },
    { 0x00000080UL, "COMPRESSION" },
    { 0x00000100UL, "ENABLEWINDOWSKEY" },
    { 0x00004000UL, "FORCE_ENCRYPTED_CS_PDU" }
};

#define N_INFO_NAMES (sizeof(info_names) / sizeof(info_names[0]))

static void flag_names(char *out, unsigned long f)
{
    unsigned int i;

    out[0] = '\0';
    for (i = 0; i < N_INFO_NAMES; i++) {
        if (f & info_names[i].bit) {
            if (out[0]) {
                strcat(out, " ");
            }
            strcat(out, info_names[i].name);
        }
    }
    if (out[0] == '\0') {
        strcpy(out, "none");
    }
}

static void read_text(INS *in, unsigned int cb, unsigned int z, char *out,
                      unsigned int cap)
{
    unsigned int i, c, n = 0;

    for (i = 0; i + z <= cb; i += z) {
        c = z == 2 ? in_u16le(in) : in_u8(in);
        if (n + 1 < cap) {
            out[n++] = (c >= 0x20 && c < 0x7F) ? (char) c : '?';
        }
    }
    out[n] = '\0';
    in_skip(in, cb - i + z);
}

#ifdef NTRDPS_LOGIN
static void to_wide(const unsigned char *b, unsigned int cb, unsigned int z,
                    WCHAR *out, unsigned int cap)
{
    unsigned int i, n = 0;

    if (z == 2) {
        for (i = 0; i + 2 <= cb && n + 1 < cap; i += 2) {
            out[n] = (WCHAR) (b[i] | (b[i + 1] << 8));
            if (out[n] == 0) {
                break;
            }
            n++;
        }
    } else {
        for (i = 0; i < cb && b[i] != 0; i++) {
        }
        n = i > 0 ? (unsigned int) MultiByteToWideChar(CP_ACP, 0,
                                                       (LPCSTR) b, (int) i,
                                                       out, (int) cap - 1)
                  : 0;
    }
    out[n] = 0;
}

static void keep_text(const INS *in, unsigned int cb, unsigned int z,
                      WCHAR *out, unsigned int cap)
{
    out[0] = 0;
    if (in_left(in) >= cb) {
        to_wide(in_ptr(in), cb, z, out, cap);
    }
}
#endif

static int read_info(SESSION *s, INS *in)
{
    unsigned int len = in_left(in);
    unsigned int cb[5], z, i;
    unsigned long need;
    char domain[64], shell[64], dir[64], names[200];

    if (len < 18) {
        session_end(s, 0, "protocol error: Client Info of %u bytes; it has "
                    "at least 18 (rdp-1 6.1 rule 1)", len);
        return 0;
    }
    in_skip(in, 4);
    s->info_flags = in_u32le(in);
    for (i = 0; i < 5; i++) {
        cb[i] = in_u16le(in);
    }
    z = (s->info_flags & INFO_UNICODE) ? 2 : 1;
    need = 18UL + cb[0] + cb[1] + cb[2] + cb[3] + cb[4] + 5 * z;

    domain[0] = shell[0] = dir[0] = s->user[0] = '\0';
    if (len < need) {
        log_msg(LOG_WARN, "session %s: bad Client Info: %u bytes, but its "
                "sizes need %lu (rdp-1 6.1 rule 4); the user name stays "
                "empty", s->peer, len, need);
    } else {
#ifdef NTRDPS_LOGIN
        keep_text(in, cb[0], z, s->ci_domain, AUTH_NAME_MAX + 1);
#endif
        read_text(in, cb[0], z, domain, sizeof(domain));
#ifdef NTRDPS_LOGIN
        keep_text(in, cb[1], z, s->ci_user, AUTH_NAME_MAX + 1);
#endif
        read_text(in, cb[1], z, s->user, sizeof(s->user));
        in_skip(in, cb[2] + z);
        read_text(in, cb[3], z, shell, sizeof(shell));
        read_text(in, cb[4], z, dir, sizeof(dir));
    }

    flag_names(names, s->info_flags);
#ifdef NTRDPS_LOGIN
    log_msg(LOG_INFO, "session %s: Client Info: user \"%s\", domain \"%s\"%s, "
            "flags 0x%08lx (%s)", s->peer, s->user, domain,
            cb[2] ? ", a password (for the login only, never logged)" : "",
            s->info_flags, names);
#else
    log_msg(LOG_INFO, "session %s: Client Info: user \"%s\", domain \"%s\"%s, "
            "flags 0x%08lx (%s)", s->peer, s->user, domain,
            cb[2] ? ", a password (not used, not kept)" : "",
            s->info_flags, names);
#endif
    if (s->info_flags & INFO_COMPRESSION) {
        log_msg(LOG_INFO, "session %s: the client takes bulk compression "
                "type %lu (%s)", s->peer, INFO_COMPR_TYPE(s->info_flags),
                INFO_COMPR_TYPE(s->info_flags) == 0 ? "8 KB" :
                INFO_COMPR_TYPE(s->info_flags) == 1 ? "64 KB" :
                "RDP 6 or later; it gets type 1");
        if (s->mppc == NULL) {
            share_start_compression(s, (int) INFO_COMPR_TYPE(s->info_flags));
        }
    }
    if (shell[0] || dir[0]) {
        log_msg(LOG_INFO, "session %s: the client asks to start \"%s\" in "
                "\"%s\"; ignored (this is the console)", s->peer, shell,
                dir);
    }
    log_msg(LOG_DEBUG, "session %s: Client Info extended part: %u bytes "
            "(skipped)", s->peer, len < need ? 0 : in_left(in));
    return 1;
}

int sec_client_info(SESSION *s, INS *in)
{
    unsigned int len = in_left(in);
    unsigned int flags = in_u16le(in);

    in_skip(in, 2);
    if (in->fail) {
        session_end(s, 0, "protocol error: %u bytes on channel 1003, too "
                    "short for the security header (rdp-1 5.2)", len);
        return 0;
    }
    if (flags & SEC_EXCHANGE_PKT) {
        log_msg(LOG_WARN, "session %s: the client wants encryption "
                "(security flags 0x%04x: a Security Exchange); ntrdps has "
                "none. rdesktop: use -e. FreeRDP: use /sec:rdp", s->peer,
                flags);
        mcs_send_dpum(s, MCS_RN_PROVIDER);
        session_end(s, 1, "the client wants encryption (rdp-1 11.3)");
        return 0;
    }
    if (!(flags & SEC_INFO_PKT)) {
        session_end(s, 0, "protocol error: security flags 0x%04x on the "
                    "first packet of channel 1003; expected Client Info "
                    "(0x0040, rdp-1 5.2)", flags);
        return 0;
    }
    if (flags & SEC_ENCRYPT) {
        log_msg(LOG_DEBUG, "session %s: security flags 0x%04x: SEC_ENCRYPT "
                "ignored, the Client Info is plain (rdp-1 5.2)", s->peer,
                flags);
    }
    return read_info(s, in);
}

int sec_is_license(const INS *in)
{
    INS peek = *in;
    unsigned int flags = in_u16le(&peek);

    return (flags & SEC_LICENSE_PKT) && in_u16le(&peek) == 0 && !peek.fail;
}

void sec_hide_password(unsigned char *p, unsigned int len)
{
    INS in;
    unsigned int li = p[4];
    unsigned int z, cb_domain, cb_user, cb_pass, at;

    if ((p[5] & 0xF0) != 0xF0 || li < 2 || 5 + li > len) {
        return;
    }
    in_init(&in, p + 5 + li, len - 5 - li);
    if (in_u8(&in) != 0x64) {
        return;
    }
    in_skip(&in, 2);
    if (in_u16be(&in) != MCS_IO_CHANNEL) {
        return;
    }
    in_skip(&in, 1);
    per_read_len(&in);
    if (!(in_u16le(&in) & SEC_INFO_PKT)) {
        return;
    }
    in_skip(&in, 2 + 4);
    z = (in_u32le(&in) & INFO_UNICODE) ? 2 : 1;
    cb_domain = in_u16le(&in);
    cb_user = in_u16le(&in);
    cb_pass = in_u16le(&in);
    in_skip(&in, 4 + cb_domain + z + cb_user + z);
    if (in.fail) {
        return;
    }
    at = 5 + li + in.pos;
    memset(p + at, 0, cb_pass < len - at ? cb_pass : len - at);
}

#ifdef NTRDPS_LOGIN
void sec_take_password(SESSION *s, unsigned char *p, unsigned int len)
{
    INS in;
    unsigned int li = p[4];
    unsigned int z, cb_domain, cb_user, cb_pass, at, n;

    if ((p[5] & 0xF0) != 0xF0 || li < 2 || 5 + li > len) {
        return;
    }
    in_init(&in, p + 5 + li, len - 5 - li);
    if (in_u8(&in) != 0x64) {
        return;
    }
    in_skip(&in, 2);
    if (in_u16be(&in) != MCS_IO_CHANNEL) {
        return;
    }
    in_skip(&in, 1);
    per_read_len(&in);
    if (!(in_u16le(&in) & SEC_INFO_PKT)) {
        return;
    }
    in_skip(&in, 2 + 4);
    z = (in_u32le(&in) & INFO_UNICODE) ? 2 : 1;
    cb_domain = in_u16le(&in);
    cb_user = in_u16le(&in);
    cb_pass = in_u16le(&in);
    in_skip(&in, 4 + cb_domain + z + cb_user + z);
    if (in.fail) {
        return;
    }
    at = 5 + li + in.pos;
    n = cb_pass < len - at ? cb_pass : len - at;
    if (s->state == ST_WAIT_JOINS) {
        to_wide(p + at, n, z, s->ci_pw, AUTH_PW_MAX + 1);
    }
    memset(p + at, 0, n);
}
#endif
