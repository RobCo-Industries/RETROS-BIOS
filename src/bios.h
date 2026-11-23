/*
 * Shared state and output of the hardware side of the BIOS: main.c,
 * post_hw.c and maint.c. Not used by the pure files.
 */
#ifndef RETROS_BIOS_H
#define RETROS_BIOS_H

#include <stdint.h>

#include <robco/blk.h>
#include <robco/klibc.h>
#include <robco/part.h>

#include "post.h"

/* Prints one marker line on the PL011. fmt starts after "ev=". */
#define BIOS_MARK(...) bios_mark(__VA_ARGS__)
void bios_mark(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Shows one line on the screen console and the PL011. */
void bios_say(const char *line);

/* Boot phases the maintenance port reports. */
#define BIOS_PHASE_INIT     0
#define BIOS_PHASE_POST     1
#define BIOS_PHASE_KEYS     2
#define BIOS_PHASE_LOAD     3
#define BIOS_PHASE_HALTED   4

typedef struct {
    int phase;              /* BIOS_PHASE_* */
    int error;              /* BIOS_ERR_* that stopped the boot, or 0 */
    int shell_requested;    /* a key came during the key window */
    int fb_ok;              /* fb_init() and console_init() succeeded */
    int post_fatal;         /* post_fatal() of the table */
    post_table_t post;
    part_info_t part;       /* the FAT partition, valid when POST_FAT is ok */
    blk_dev_t part_dev;
    int mounted;            /* the FAT volume is mounted as "sd" */
} bios_state_t;

extern bios_state_t bios;

/* Runs every probe, fills bios.post, prints one marker per probe. */
void post_run(void);

/* Starts the input backends and the maintenance port commands. */
void maint_start(void);
/* Stops for good: answers the maintenance port, never returns. */
void maint_idle(void) __attribute__((noreturn));

#endif
