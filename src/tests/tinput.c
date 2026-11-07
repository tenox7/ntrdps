/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <string.h>
#include "input.h"
#include "sas.h"
#include "desktop.h"
#include "prompt.h"

int          t_input_calls;
unsigned int t_input_locks;
int          t_input_releases;

PROMPT_DONE  t_prompt_done;
void        *t_prompt_ctx;
int          t_prompt_open;
char         t_prompt_address[64];
unsigned int t_prompt_timeout;

char         t_input_desktop[64] = "Default";

static int   g_prompt;

void input_key(unsigned int code, unsigned int prefix, int down)
{
    t_input_calls++;
}

void input_locks(unsigned int locks)
{
    t_input_calls++;
    t_input_locks = locks;
}

void input_mouse(int x, int y, unsigned int buttons, int wheel)
{
    t_input_calls++;
}

void input_release(void)
{
    t_input_releases++;
}

void sas_send(void)
{
}

void *prompt_ask(const char *address, unsigned int timeout_s,
                 PROMPT_DONE done, void *ctx)
{
    t_prompt_done = done;
    t_prompt_ctx = ctx;
    t_prompt_open++;
    strncpy(t_prompt_address, address, sizeof(t_prompt_address) - 1);
    t_prompt_timeout = timeout_s;
    return &g_prompt;
}

void prompt_end(void *prompt)
{
    t_prompt_open--;
    t_prompt_done = NULL;
}

int desktop_input_name(char *name, unsigned int size)
{
    strncpy(name, t_input_desktop, size - 1);
    name[size - 1] = '\0';
    return name[0] != '\0';
}
