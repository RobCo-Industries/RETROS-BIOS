#include "settings.h"

#include <robco/console.h>
#include <robco/ini.h>
#include <robco/klibc.h>

static const char *const scheme_names[] = { "green", "amber", "blue", "white" };
#define SCHEME_COUNT (sizeof(scheme_names) / sizeof(scheme_names[0]))

typedef struct {
    bios_settings_t s;
    bios_cfg_report_t r;
} parse_ctx_t;

static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

static int same_word(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (lower((unsigned char)*a) != lower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/*
 * A number is one or more ASCII digits and nothing else: no sign, no
 * space, no unit. Leading zeros are fine. A value past 2^32 - 1
 * saturates there. Returns 0, or -1 when the text is not a number.
 */
static int parse_uint(const char *text, uint32_t *out)
{
    uint32_t v = 0;
    uint32_t d;

    if (*text == '\0') {
        return -1;
    }
    for (; *text != '\0'; text++) {
        if (*text < '0' || *text > '9') {
            return -1;
        }
        d = (uint32_t)(*text - '0');
        v = v > (UINT32_MAX - d) / 10u ? UINT32_MAX : v * 10u + d;
    }
    *out = v;
    return 0;
}

static int parse_bool(const char *text, int *out)
{
    static const char *const yes[] = { "on", "yes", "true", "1" };
    static const char *const no[] = { "off", "no", "false", "0" };
    unsigned i;

    for (i = 0; i < 4; i++) {
        if (same_word(text, yes[i])) {
            *out = 1;
            return 0;
        }
        if (same_word(text, no[i])) {
            *out = 0;
            return 0;
        }
    }
    return -1;
}

int bios_scheme_from_name(const char *name)
{
    unsigned i;

    for (i = 0; i < SCHEME_COUNT; i++) {
        if (same_word(name, scheme_names[i])) {
            return (int)i;
        }
    }
    return -1;
}

const char *bios_scheme_name(unsigned scheme)
{
    return scheme < SCHEME_COUNT ? scheme_names[scheme] : "unknown";
}

void bios_settings_default(bios_settings_t *s)
{
    s->delay_ms = BIOS_DELAY_DEFAULT_MS;
    s->beep = 1;
    s->scheme = CONSOLE_SCHEME_GREEN;
}

static int on_key(const char *section, const char *key, const char *value, unsigned line,
                  void *arg)
{
    parse_ctx_t *c = arg;
    uint32_t n;
    int b;
    int scheme;

    (void)line;
    if (!same_word(section, "bios")) {
        return 0;
    }
    if (same_word(key, "delay_ms")) {
        if (parse_uint(value, &n) != 0) {
            c->r.bad++;
            return 0;
        }
        if (n > BIOS_DELAY_MAX_MS) {
            n = BIOS_DELAY_MAX_MS;
            c->r.clamped++;
        }
        c->s.delay_ms = n;
    } else if (same_word(key, "beep")) {
        if (parse_bool(value, &b) != 0) {
            c->r.bad++;
            return 0;
        }
        c->s.beep = b;
    } else if (same_word(key, "scheme")) {
        scheme = bios_scheme_from_name(value);
        if (scheme < 0) {
            c->r.bad++;
            return 0;
        }
        c->s.scheme = (unsigned)scheme;
    } else {
        c->r.unknown++;
        return 0;
    }
    c->r.applied++;
    return 0;
}

int bios_settings_parse(const char *text, size_t len, bios_settings_t *s,
                        bios_cfg_report_t *report, unsigned *err_line)
{
    parse_ctx_t c;
    unsigned line = 0;
    int rc;

    c.s = *s;
    k_memset(&c.r, 0, sizeof(c.r));
    rc = ini_parse(text, len, on_key, &c, &line);
    if (err_line != NULL) {
        *err_line = line;
    }
    if (rc < 0) {
        bios_settings_default(s);
        if (report != NULL) {
            k_memset(report, 0, sizeof(*report));
        }
        return rc;
    }
    *s = c.s;
    if (report != NULL) {
        *report = c.r;
    }
    return 0;
}
