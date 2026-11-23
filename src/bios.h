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
#include "settings.h"

/* Prints one marker line on the PL011. fmt starts after "ev=". */
#define BIOS_MARK(...) bios_mark(__VA_ARGS__)
void bios_mark(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Shows one line on the screen console and the PL011. */
void bios_say(const char *line);
/* Writes text on the screen console and the PL011, no line end added. */
void bios_put(const char *text);

/* Boot phases the maintenance port reports. */
#define BIOS_PHASE_INIT     0
#define BIOS_PHASE_POST     1
#define BIOS_PHASE_KEYS     2
#define BIOS_PHASE_LOAD     3
#define BIOS_PHASE_HALTED   4
#define BIOS_PHASE_SHELL    5

typedef struct {
    int phase;              /* BIOS_PHASE_* */
    int error;              /* BIOS_ERR_* that stopped the boot, or 0 */
    int shell_requested;    /* a key came during the key window, or `shell` on the port */
    int shell_maint;        /* `shell` on the maintenance port asked for the shell */
    int prompt_open;        /* the PL011 line holds a prompt: a marker starts a new line */
    int fb_ok;              /* fb_init() and console_init() succeeded */
    int post_fatal;         /* post_fatal() of the table */
    int timer_bad;          /* the timer probe ran and did not pass: nothing may wait on time */
    post_table_t post;
    part_info_t part;       /* the FAT partition, valid when POST_FAT is ok */
    blk_dev_t part_dev;
    int mounted;            /* the FAT volume is mounted as "sd" */
    bios_settings_t settings;   /* in force */
    const char *cfg_source;     /* BIOS_CFG_PATH or "defaults" */
} bios_state_t;

extern bios_state_t bios;

/* Runs every probe, fills bios.post, prints one marker per probe. */
void post_run(void);

/*
 * Runs the self test again and prints the table. The FAT probe unmounts
 * the earlier volume first, or reports why it cannot.
 */
void bios_post_table(void);

/*
 * Loads a second stage and enters it. path NULL: MFBOOT.SYS with the
 * fallback to MFBOOT.BAK. Otherwise exactly that file. Returns the error
 * code after showing it; never returns on success.
 */
int bios_boot(const char *path);

/*
 * The emergency shell. reason is the marker text: "key", "maint" or an
 * error name. from_keys: CONTINUE may go on with the boot. Returns only
 * for CONTINUE.
 */
void shell_run(const char *reason, int from_keys);
/* Turns the pager on or off: the maintenance command `paging`. */
void shell_set_paging(int on);

/* Starts the input backends and the maintenance port commands. */
void maint_start(void);
/* Stops for good: answers the maintenance port, never returns. */
void maint_idle(void) __attribute__((noreturn));

#endif
