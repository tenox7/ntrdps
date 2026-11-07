/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_PROMPT_H
#define NTRDPS_PROMPT_H

#define PROMPT_YES     1
#define PROMPT_NO      2
#define PROMPT_TIMEOUT 3
#define PROMPT_FAILED  4

typedef void (*PROMPT_DONE)(void *ctx, int answer);

void *prompt_ask(const char *address, unsigned int timeout_s,
                 PROMPT_DONE done, void *ctx);

void prompt_end(void *prompt);

#endif
