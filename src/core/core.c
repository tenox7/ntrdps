/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "core.h"
#include "input.h"
#include "sas.h"
#include "desktop.h"
#include "prompt.h"
#include "gstore.h"
#include "bstore.h"

typedef struct SESS {
    struct SESS        *next;
    const FRONTEND_API *fe;
    void               *fe_ctx;
    char                peer[32];
    int                 view_only;
    int                 dropped;
    int                 ready;
    volatile int        ending;
    volatile int        waiting;
    void               *prompt;
    unsigned int        locks;
    int                 locks_due;
} SESS;

typedef struct {
    CRITICAL_SECTION    run_lock;
    CRITICAL_SECTION    sess_lock;
    CRITICAL_SECTION    frame_lock;
    const CORE_BACKEND *backend;
    int                 running;
    CORE_FRAME          frame;
    CORE_POINTER        pointer;
    unsigned long       serial;
    SESS               *sessions;
    int                 open;
    unsigned int        prompt_s;
    int                 refuse;
    CORE_ORDER          olog[CORE_ORDER_LOG];
    unsigned long       onext;
    unsigned long       okept;
    int                 onew;
    int                 onew_pix;
    RECT                onew_box;
    GSTORE             *glyphs;
    BSTORE             *bitmaps;
} CORE;

static CORE g_core;

static int start_backend(CORE *c)
{
    CORE_FRAME f;

    EnterCriticalSection(&c->frame_lock);
    memset(&c->pointer, 0, sizeof(c->pointer));
    c->pointer.cb = sizeof(c->pointer);
    LeaveCriticalSection(&c->frame_lock);
    memset(&f, 0, sizeof(f));
    f.cb = sizeof(f);
    if (!c->backend->start(&f)) {
        log_msg(LOG_ERROR, "core: the back-end \"%s\" did not start",
                c->backend->name);
        return 0;
    }
    EnterCriticalSection(&c->frame_lock);
    c->frame = f;
    c->running = 1;
    if (c->bitmaps != NULL) {
        bstore_clear(c->bitmaps);
    }
    LeaveCriticalSection(&c->frame_lock);
    log_msg(LOG_INFO, "core: back-end \"%s\": %u x %u, %u bpp",
            c->backend->name, f.width, f.height, f.bpp);
    return 1;
}

static void stop_backend(CORE *c)
{
    EnterCriticalSection(&c->frame_lock);
    c->running = 0;
    LeaveCriticalSection(&c->frame_lock);
    c->backend->stop();
    log_msg(LOG_DEBUG, "core: back-end \"%s\" stopped", c->backend->name);
}

static int prompt_needed(const char *peer)
{
    char desk[64];

    if (desktop_input_name(desk, sizeof(desk)) &&
        strcmp(desk, "Winlogon") == 0) {
        log_msg(LOG_INFO, "core: session %s: no console prompt: the console "
                "shows the Winlogon desktop, so Windows asks for a password",
                peer);
        return 0;
    }
    return 1;
}

static int replace_others(CORE *c, SESS *s, char *old)
{
    SESS *o;
    int n = 0;

    for (o = c->sessions; o != NULL; o = o->next) {
        if (o != s && o->ready && !o->ending) {
            o->ending = 1;
            o->fe->Stop(o->fe_ctx, CORE_STOP_REPLACED);
            strcpy(old, o->peer);
            n++;
        }
    }
    return n;
}

static void prompt_done(void *ctx, int answer)
{
    CORE *c = &g_core;
    SESS *s = (SESS *) ctx;
    unsigned int locks = 0;
    int locks_due = 0, replaced = 0;
    char old[32];

    switch (answer) {
    case PROMPT_YES:
        log_msg(LOG_INFO, "core: session %s: the console user said yes",
                s->peer);
        break;
    case PROMPT_NO:
        log_msg(LOG_INFO, "core: session %s: the console user said no",
                s->peer);
        break;
    case PROMPT_TIMEOUT:
        log_msg(LOG_INFO, "core: session %s: the console user did not "
                "answer within %u s: that means no", s->peer, c->prompt_s);
        break;
    default:
        log_msg(LOG_WARN, "core: session %s: the console prompt could not "
                "be shown, so the session is refused", s->peer);
        break;
    }
    EnterCriticalSection(&c->sess_lock);
    if (answer == PROMPT_YES) {
        replaced = replace_others(c, s, old);
        s->waiting = 0;
        s->dropped = 0;
        locks = s->locks;
        locks_due = s->locks_due;
        s->fe->Hold(s->fe_ctx, 0);
    } else {
        s->ending = 1;
        s->fe->Stop(s->fe_ctx, CORE_STOP_DENIED);
    }
    LeaveCriticalSection(&c->sess_lock);
    if (replaced) {
        log_msg(LOG_INFO, "core: session %s replaces session %s, which had "
                "the console", s->peer, old);
        input_release();
    }
    if (locks_due && !s->view_only) {
        input_locks(locks);
    }
}

static void *__cdecl session_open(void *core, const FRONTEND_API *fe,
                                  void *fe_ctx, const char *peer,
                                  unsigned int flags)
{
    CORE *c = (CORE *) core;
    SESS *s;

    if (fe == NULL || fe->cb < sizeof(FRONTEND_API)) {
        log_msg(LOG_ERROR, "core: session %s: the front-end table is too "
                "short", peer);
        return NULL;
    }
    s = (SESS *) calloc(1, sizeof(SESS));
    if (s == NULL) {
        log_msg(LOG_ERROR, "core: session %s: out of memory", peer);
        return NULL;
    }
    s->fe = fe;
    s->fe_ctx = fe_ctx;
    strncpy(s->peer, peer, sizeof(s->peer) - 1);
    s->view_only = (flags & CORE_VIEW_ONLY) != 0;

    EnterCriticalSection(&c->run_lock);
    if (c->open == 0 && !start_backend(c)) {
        LeaveCriticalSection(&c->run_lock);
        free(s);
        return NULL;
    }
    EnterCriticalSection(&c->sess_lock);
    s->next = c->sessions;
    c->sessions = s;
    c->open++;
    LeaveCriticalSection(&c->sess_lock);
    LeaveCriticalSection(&c->run_lock);
    log_msg(LOG_DEBUG, "core: session %s open%s", s->peer,
            s->view_only ? ", view-only" : "");
    return s;
}

static int __cdecl session_ready(void *core, void *session)
{
    CORE *c = (CORE *) core;
    SESS *s = (SESS *) session, *o, *other = NULL;
    char old[32];
    int ask, busy = 0, replaced = 0;

    ask = c->prompt_s > 0 && prompt_needed(s->peer);
    old[0] = '\0';
    EnterCriticalSection(&c->sess_lock);
    for (o = c->sessions; o != NULL; o = o->next) {
        if (o != s && o->ready && !o->ending &&
            (other == NULL || o->waiting)) {
            other = o;
        }
    }
    if (other != NULL) {
        strcpy(old, other->peer);
        busy = other->waiting ? 1 : c->refuse ? 2 : 0;
    }
    if (busy) {
        s->ending = 1;
    } else {
        s->ready = 1;
        s->waiting = ask;
        if (ask) {
            s->fe->Hold(s->fe_ctx, 1);
        } else {
            replaced = replace_others(c, s, old);
        }
    }
    LeaveCriticalSection(&c->sess_lock);

    if (busy == 1) {
        log_msg(LOG_INFO, "core: session %s refused: session %s waits for "
                "the console user (one prompt at a time)", s->peer, old);
        return CORE_STOP_BUSY;
    }
    if (busy == 2) {
        log_msg(LOG_INFO, "core: session %s refused: session %s has the "
                "console (NewClient = 1)", s->peer, old);
        return CORE_STOP_BUSY;
    }
    if (replaced) {
        log_msg(LOG_INFO, "core: session %s replaces session %s, which had "
                "the console (NewClient = 0)", s->peer, old);
        input_release();
    }
    if (ask) {
        if (other != NULL) {
            log_msg(LOG_INFO, "core: session %s waits for the console user; "
                    "session %s keeps the console until the answer",
                    s->peer, old);
        }
        s->prompt = prompt_ask(s->peer, c->prompt_s, prompt_done, s);
        if (s->prompt == NULL) {
            prompt_done(s, PROMPT_FAILED);
        }
    }
    return 0;
}

static void __cdecl session_close(void *core, void *session)
{
    CORE *c = (CORE *) core;
    SESS **pp;
    int found = 0;

    if (((SESS *) session)->prompt != NULL) {
        prompt_end(((SESS *) session)->prompt);
        ((SESS *) session)->prompt = NULL;
    }
    EnterCriticalSection(&c->run_lock);
    EnterCriticalSection(&c->sess_lock);
    for (pp = &c->sessions; *pp != NULL; pp = &(*pp)->next) {
        if (*pp == (SESS *) session) {
            *pp = (*pp)->next;
            c->open--;
            found = 1;
            break;
        }
    }
    LeaveCriticalSection(&c->sess_lock);
    if (!found) {
        LeaveCriticalSection(&c->run_lock);
        log_msg(LOG_ERROR, "core: SessionClose for a session that is not "
                "open");
        return;
    }
    log_msg(LOG_DEBUG, "core: session %s closed", ((SESS *) session)->peer);
    if (((SESS *) session)->ready && !((SESS *) session)->view_only &&
        !((SESS *) session)->waiting && !((SESS *) session)->ending) {
        input_release();
    }
    if (c->open == 0) {
        stop_backend(c);
    }
    LeaveCriticalSection(&c->run_lock);
    free(session);
}

static void __cdecl session_view_only(void *core, void *session, int on)
{
    SESS *s = (SESS *) session;

    if (s->view_only != (on != 0)) {
        s->view_only = on != 0;
        s->dropped = 0;
        log_msg(LOG_INFO, "core: session %s: view-only %s", s->peer,
                on ? "on" : "off");
        if (on) {
            input_release();
        }
    }
}

static int __cdecl frame_lock(void *core, CORE_FRAME *f)
{
    CORE *c = (CORE *) core;
    unsigned long cb = f->cb;

    EnterCriticalSection(&c->frame_lock);
    if (!c->running) {
        LeaveCriticalSection(&c->frame_lock);
        return 0;
    }
    memcpy(f, &c->frame, cb < sizeof(CORE_FRAME) ? cb : sizeof(CORE_FRAME));
    f->cb = cb;
    return 1;
}

static void __cdecl frame_unlock(void *core)
{
    LeaveCriticalSection(&((CORE *) core)->frame_lock);
}

static int __cdecl orders_get(void *core, unsigned long *next,
                              CORE_ORDER *out, int max)
{
    CORE *c = (CORE *) core;
    unsigned long n = *next;
    int k = 0;

    if (max <= 0) {
        *next = c->onext;
        return 0;
    }
    if (c->onext - n > c->okept) {
        *next = c->onext;
        return -1;
    }
    while (n != c->onext && k < max) {
        out[k++] = c->olog[n % CORE_ORDER_LOG];
        n++;
    }
    *next = n;
    return k;
}

static int __cdecl text_get(void *core, const CORE_ORDER *o,
                            CORE_GLYPHREF *refs)
{
    CORE *c = (CORE *) core;

    if (c->glyphs == NULL || o->kind != CORE_ORDER_TEXT ||
        o->nglyph < 1 || o->nglyph > CORE_TEXT_MAX) {
        return 0;
    }
    return gstore_run(c->glyphs, o->run, o->nglyph, refs);
}

static const CORE_GLYPH *__cdecl glyph_get(void *core, unsigned int id,
                                           unsigned long serial)
{
    CORE *c = (CORE *) core;

    return c->glyphs == NULL ? NULL : gstore_get(c->glyphs, id, serial);
}

static const CORE_BITMAP *__cdecl bitmap_get(void *core, unsigned int id,
                                             unsigned long serial)
{
    CORE *c = (CORE *) core;

    return c->bitmaps == NULL ? NULL : bstore_get(c->bitmaps, id, serial);
}

static int __cdecl pointer_get(void *core, CORE_POINTER *p)
{
    CORE *c = (CORE *) core;
    unsigned long cb = p->cb;
    int ok;

    EnterCriticalSection(&c->frame_lock);
    ok = c->running;
    if (ok) {
        memcpy(p, &c->pointer,
               cb < sizeof(CORE_POINTER) ? cb : sizeof(CORE_POINTER));
        p->cb = cb;
    }
    LeaveCriticalSection(&c->frame_lock);
    return ok;
}

static int may_input(SESS *s)
{
    if (!s->ready || s->ending) {
        return 0;
    }
    if (s->waiting) {
        if (!s->dropped) {
            s->dropped = 1;
            log_msg(LOG_INFO, "core: session %s waits for the console "
                    "user: its input is dropped", s->peer);
        }
        return 0;
    }
    if (!s->view_only) {
        return 1;
    }
    if (!s->dropped) {
        s->dropped = 1;
        log_msg(LOG_INFO, "core: session %s is view-only: its input is "
                "dropped", s->peer);
    }
    return 0;
}

static void __cdecl key(void *core, void *session, unsigned int sc,
                        unsigned int prefix, int down)
{
    if (!may_input((SESS *) session)) {
        return;
    }
    input_key(sc, prefix, down);
}

static void __cdecl key_locks(void *core, void *session, unsigned int locks)
{
    CORE *c = (CORE *) core;
    SESS *s = (SESS *) session;
    int kept = 0;

    if (s->waiting) {
        EnterCriticalSection(&c->sess_lock);
        if (s->waiting) {
            s->locks = locks;
            s->locks_due = 1;
            kept = 1;
        }
        LeaveCriticalSection(&c->sess_lock);
    }
    if (kept || !may_input(s)) {
        return;
    }
    input_locks(locks);
}

static void __cdecl mouse(void *core, void *session, int x, int y,
                          unsigned int buttons, int wheel)
{
    if (!may_input((SESS *) session)) {
        return;
    }
    input_mouse(x, y, buttons, wheel);
}

static void __cdecl secure_attention(void *core, void *session)
{
    if (!may_input((SESS *) session)) {
        return;
    }
    sas_send();
}

static void __cdecl core_log(void *core, int level, const char *msg)
{
    log_msg(level, "%s", msg);
}

const CORE_API core_api = {
    sizeof(CORE_API), CORE_VERSION,
    session_open, session_close, session_view_only,
    frame_lock, frame_unlock,
    pointer_get,
    key, key_locks, mouse, secure_attention,
    core_log,
    session_ready,
    orders_get,
    text_get, glyph_get,
    bitmap_get
};

void *core_start(const CORE_BACKEND *backend)
{
    CORE *c = &g_core;

    memset(c, 0, sizeof(*c));
    InitializeCriticalSection(&c->run_lock);
    InitializeCriticalSection(&c->sess_lock);
    InitializeCriticalSection(&c->frame_lock);
    c->backend = backend;
    c->pointer.cb = sizeof(c->pointer);
    return c;
}

void core_set_prompt(unsigned int timeout_s)
{
    g_core.prompt_s = timeout_s;
}

void core_set_new_client(int refuse)
{
    g_core.refuse = refuse != 0;
}

void core_stop(void *core)
{
    CORE *c = (CORE *) core;

    if (c->open != 0) {
        log_msg(LOG_ERROR, "core: stop with %d session%s still open",
                c->open, c->open == 1 ? "" : "s");
        return;
    }
    DeleteCriticalSection(&c->frame_lock);
    DeleteCriticalSection(&c->sess_lock);
    DeleteCriticalSection(&c->run_lock);
    gstore_free(c->glyphs);
    c->glyphs = NULL;
    bstore_free(c->bitmaps);
    c->bitmaps = NULL;
}

void core_frame_enter(void)
{
    EnterCriticalSection(&g_core.frame_lock);
}

static void orders_told(CORE *c, const RECT *box, unsigned long at)
{
    SESS *s;

    EnterCriticalSection(&c->sess_lock);
    for (s = c->sessions; s != NULL; s = s->next) {
        if (s->fe->Orders != NULL) {
            s->fe->Orders(s->fe_ctx);
        } else if (box != NULL) {
            s->fe->Damage(s->fe_ctx, box, 1, at);
        }
    }
    LeaveCriticalSection(&c->sess_lock);
}

void core_frame_leave(void)
{
    CORE *c = &g_core;
    RECT box = c->onew_box;
    unsigned long at = c->onext;
    int news = c->onew, pix = c->onew_pix;

    c->onew = 0;
    c->onew_pix = 0;
    LeaveCriticalSection(&c->frame_lock);
    if (news) {
        orders_told(c, pix ? &box : NULL, at);
    }
}

void core_order(const CORE_ORDER *o)
{
    CORE *c = &g_core;

    c->onew = 1;
    if (o->kind == CORE_ORDER_SAVE || o->kind == CORE_ORDER_FREE) {
    } else if (!c->onew_pix) {
        c->onew_pix = 1;
        c->onew_box = o->rect;
    } else {
        if (o->rect.left < c->onew_box.left) {
            c->onew_box.left = o->rect.left;
        }
        if (o->rect.top < c->onew_box.top) {
            c->onew_box.top = o->rect.top;
        }
        if (o->rect.right > c->onew_box.right) {
            c->onew_box.right = o->rect.right;
        }
        if (o->rect.bottom > c->onew_box.bottom) {
            c->onew_box.bottom = o->rect.bottom;
        }
    }
    c->olog[c->onext % CORE_ORDER_LOG] = *o;
    c->onext++;
    if (c->okept < CORE_ORDER_LOG) {
        c->okept++;
    }
}

int core_glyph_put(const CORE_GLYPH *g, unsigned long *serial)
{
    CORE *c = &g_core;

    if (c->glyphs == NULL) {
        c->glyphs = gstore_new();
        if (c->glyphs == NULL) {
            return -1;
        }
    }
    return (int) gstore_put(c->glyphs, g, serial);
}

const CORE_GLYPH *core_glyph_get(unsigned int id, unsigned long serial)
{
    CORE *c = &g_core;

    return c->glyphs == NULL ? NULL : gstore_get(c->glyphs, id, serial);
}

unsigned long core_text_put(const CORE_GLYPHREF *r, int n)
{
    CORE *c = &g_core;

    return c->glyphs == NULL ? 0 : gstore_log(c->glyphs, r, n);
}

int core_bitmap_put(int cx, int cy, unsigned int bpp,
                    const unsigned char *bits, long stride,
                    unsigned long *serial)
{
    CORE *c = &g_core;

    if (c->bitmaps == NULL) {
        c->bitmaps = bstore_new();
        if (c->bitmaps == NULL) {
            return -1;
        }
    }
    return bstore_put(c->bitmaps, cx, cy, bpp, bits, stride, serial);
}

void core_frame_format(const CORE_FRAME *f)
{
    CORE *c = &g_core;

    if (f == NULL) {
        c->running = 0;
        log_msg(LOG_ERROR, "core: the back-end \"%s\" lost its frame",
                c->backend->name);
        return;
    }
    c->frame = *f;
    c->frame.cb = sizeof(c->frame);
    c->okept = 0;
    if (c->bitmaps != NULL) {
        bstore_clear(c->bitmaps);
    }
    log_msg(LOG_INFO, "core: back-end \"%s\": the console changed to %u x "
            "%u, %u bpp", c->backend->name, f->width, f->height, f->bpp);
}

void core_frame_changed(void)
{
    CORE *c = &g_core;
    SESS *s;

    EnterCriticalSection(&c->sess_lock);
    for (s = c->sessions; s != NULL; s = s->next) {
        s->fe->FrameChanged(s->fe_ctx);
    }
    LeaveCriticalSection(&c->sess_lock);
}

void core_pointer_shape(const CORE_POINTER *p)
{
    CORE *c = &g_core;
    SESS *s;
    int x, y;

    EnterCriticalSection(&c->frame_lock);
    x = c->pointer.x;
    y = c->pointer.y;
    if (p == NULL) {
        memset(&c->pointer, 0, sizeof(c->pointer));
    } else {
        c->pointer = *p;
        if (++c->serial == 0) {
            c->serial = 1;
        }
        c->pointer.serial = c->serial;
    }
    c->pointer.cb = sizeof(c->pointer);
    c->pointer.x = x;
    c->pointer.y = y;
    LeaveCriticalSection(&c->frame_lock);

    EnterCriticalSection(&c->sess_lock);
    for (s = c->sessions; s != NULL; s = s->next) {
        s->fe->PointerShape(s->fe_ctx);
    }
    LeaveCriticalSection(&c->sess_lock);
}

void core_pointer_pos(int x, int y)
{
    CORE *c = &g_core;
    SESS *s;

    EnterCriticalSection(&c->frame_lock);
    c->pointer.x = x;
    c->pointer.y = y;
    LeaveCriticalSection(&c->frame_lock);

    EnterCriticalSection(&c->sess_lock);
    for (s = c->sessions; s != NULL; s = s->next) {
        s->fe->PointerPos(s->fe_ctx, x, y);
    }
    LeaveCriticalSection(&c->sess_lock);
}

void core_damage(const RECT *rects, int count)
{
    CORE *c = &g_core;
    SESS *s;
    unsigned long at;

    if (count <= 0) {
        return;
    }
    EnterCriticalSection(&c->frame_lock);
    at = c->onext;
    LeaveCriticalSection(&c->frame_lock);
    EnterCriticalSection(&c->sess_lock);
    for (s = c->sessions; s != NULL; s = s->next) {
        s->fe->Damage(s->fe_ctx, rects, count, at);
    }
    LeaveCriticalSection(&c->sess_lock);
}
