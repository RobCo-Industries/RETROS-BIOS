/*
 * The shell's diagnostics, on the hardware. diagcalc.c holds the pure
 * decisions. Not used by the pure files.
 */
#ifndef RETROS_DIAG_H
#define RETROS_DIAG_H

#include <stdint.h>

#include <robco/input.h>

#include "shcmd.h"

/* DIAG ALL tests this much of the default window, from its base. */
#define DIAG_ALL_MEM_SIZE 0x01000000u

/* Runs DIAG as parsed. Returns 0 when no test failed, -1 otherwise. */
int diag_command(const sh_cmd_t *c);

/* Shell output and keys, from shell.c. */
int shell_say(const char *line);
int shell_key(input_event_t *ev, uint32_t ms);
int shell_key_pending(void);

#endif
