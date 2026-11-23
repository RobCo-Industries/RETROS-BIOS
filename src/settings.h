/*
 * BIOS settings from the [bios] section of /ROBCO/BOOT.CFG. Pure code:
 * it builds and runs on the host in the unit tests.
 *
 * BOOT_PROTOCOL.md defines BOOT.CFG and the keys MFBootAgent reads. It
 * names no key for the BIOS, so the BIOS reads its own [bios] section and
 * ignores every other section.
 *
 *   [bios]
 *   delay_ms = 1000   key window before the load, 0 to 10000, clamped
 *   beep = on         power-on beep: on/off, yes/no, true/false, 1/0
 *   scheme = green    screen colours: green, amber, blue or white
 *
 * A delay_ms value is one or more ASCII digits and nothing else. Leading
 * zeros are fine. A sign, a space inside, a unit or any other character
 * makes the value bad. Any run of digits is taken: a value past 2^32 - 1
 * saturates there, then the clamp to 10000 applies.
 */
#ifndef RETROS_SETTINGS_H
#define RETROS_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

#define BIOS_CFG_PATH           "/ROBCO/BOOT.CFG"
#define BIOS_CFG_MAX            4096u
#define BIOS_DELAY_DEFAULT_MS   1000u
#define BIOS_DELAY_MAX_MS       10000u

typedef struct {
    uint32_t delay_ms;  /* key window length */
    int beep;           /* 1: power-on beep */
    unsigned scheme;    /* CONSOLE_SCHEME_* */
} bios_settings_t;

/* What parsing found. */
typedef struct {
    unsigned applied;   /* [bios] keys taken, clamped ones included */
    unsigned clamped;   /* numbers out of range, set to the nearest limit */
    unsigned bad;       /* [bios] keys with a value that is not valid, skipped */
    unsigned unknown;   /* unknown keys in [bios], skipped */
} bios_cfg_report_t;

void bios_settings_default(bios_settings_t *s);

/*
 * Applies the [bios] keys in text to *s. A bad value or an unknown key is
 * counted and skipped; the key keeps its value. On a syntax error *s is
 * set back to the defaults and the result is the ini_parse() error, with
 * the line in *err_line (if not NULL). Returns 0 otherwise. report may be
 * NULL.
 */
int bios_settings_parse(const char *text, size_t len, bios_settings_t *s,
                        bios_cfg_report_t *report, unsigned *err_line);

/* Scheme number for a name (green, amber, blue, white; any case), or -1. */
int bios_scheme_from_name(const char *name);
/* Name of a scheme number, or "unknown". */
const char *bios_scheme_name(unsigned scheme);

#endif
