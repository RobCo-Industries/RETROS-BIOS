/* [bios] settings from BOOT.CFG: defaults, clamps, bad values, bad files. */

#include <robco/console.h>
#include <robco/ini.h>

#include "rc_test.h"
#include "settings.h"

static int parse(const char *text, bios_settings_t *s, bios_cfg_report_t *r, unsigned *line)
{
    bios_settings_default(s);
    return bios_settings_parse(text, strlen(text), s, r, line);
}

TEST(defaults)
{
    bios_settings_t s;

    bios_settings_default(&s);
    CHECK_EQ(s.delay_ms, 1000);
    CHECK_EQ(s.beep, 1);
    CHECK_EQ(s.scheme, CONSOLE_SCHEME_GREEN);
}

TEST(empty_file_keeps_defaults)
{
    bios_settings_t s;
    bios_cfg_report_t r;

    CHECK_EQ(parse("", &s, &r, NULL), 0);
    CHECK_EQ(r.applied, 0);
    CHECK_EQ(s.delay_ms, 1000);
}

TEST(all_keys)
{
    bios_settings_t s;
    bios_cfg_report_t r;

    CHECK_EQ(parse("[bios]\ndelay_ms = 0\nbeep = off\nscheme = Amber\n", &s, &r, NULL), 0);
    CHECK_EQ(s.delay_ms, 0);
    CHECK_EQ(s.beep, 0);
    CHECK_EQ(s.scheme, CONSOLE_SCHEME_AMBER);
    CHECK_EQ(r.applied, 3);
    CHECK_EQ(r.bad + r.unknown + r.clamped, 0);
}

TEST(other_sections_ignored)
{
    bios_settings_t s;
    bios_cfg_report_t r;
    const char *text =
        "[boot]\ndefault = UOS\ntimeout = 3\n\n"
        "[entry UOS]\npath = /ROBCO/UOS.SYS\ncmdline =\n\n"
        "[entry MAINT]\npath = /ROBCO/MAINT.SYS\ndelay_ms = 5\n";

    CHECK_EQ(parse(text, &s, &r, NULL), 0);
    CHECK_EQ(r.applied, 0);
    CHECK_EQ(r.unknown, 0);
    CHECK_EQ(s.delay_ms, 1000);
}

TEST(delay_clamped)
{
    bios_settings_t s;
    bios_cfg_report_t r;

    CHECK_EQ(parse("[bios]\ndelay_ms = 10001\n", &s, &r, NULL), 0);
    CHECK_EQ(s.delay_ms, BIOS_DELAY_MAX_MS);
    CHECK_EQ(r.clamped, 1);
    CHECK_EQ(r.applied, 1);
    CHECK_EQ(parse("[bios]\ndelay_ms = 999999999\n", &s, &r, NULL), 0);
    CHECK_EQ(s.delay_ms, BIOS_DELAY_MAX_MS);
    CHECK_EQ(parse("[bios]\ndelay_ms = 10000\n", &s, &r, NULL), 0);
    CHECK_EQ(s.delay_ms, 10000);
    CHECK_EQ(r.clamped, 0);
}

TEST(bad_values_skipped)
{
    bios_settings_t s;
    bios_cfg_report_t r;

    CHECK_EQ(parse("[bios]\ndelay_ms = -1\nbeep = maybe\nscheme = pink\n", &s, &r, NULL), 0);
    CHECK_EQ(r.bad, 3);
    CHECK_EQ(r.applied, 0);
    CHECK_EQ(s.delay_ms, 1000);
    CHECK_EQ(s.beep, 1);
    CHECK_EQ(s.scheme, CONSOLE_SCHEME_GREEN);
    CHECK_EQ(parse("[bios]\ndelay_ms = 1234567890\n", &s, &r, NULL), 0);
    CHECK_EQ(r.bad, 1);
    CHECK_EQ(parse("[bios]\ndelay_ms =\n", &s, &r, NULL), 0);
    CHECK_EQ(r.bad, 1);
}

TEST(unknown_key_counted)
{
    bios_settings_t s;
    bios_cfg_report_t r;

    CHECK_EQ(parse("[BIOS]\nquiet = on\nBeep = no\n", &s, &r, NULL), 0);
    CHECK_EQ(r.unknown, 1);
    CHECK_EQ(r.applied, 1);
    CHECK_EQ(s.beep, 0);
}

TEST(syntax_error_resets_to_defaults)
{
    bios_settings_t s;
    bios_cfg_report_t r;
    unsigned line = 0;

    CHECK_EQ(parse("[bios]\nbeep = off\nthis is not ini\n", &s, &r, &line), INI_ERR_SYNTAX);
    CHECK_EQ(line, 3);
    CHECK_EQ(s.beep, 1);
    CHECK_EQ(r.applied, 0);
}

TEST(scheme_names)
{
    CHECK_EQ(bios_scheme_from_name("green"), 0);
    CHECK_EQ(bios_scheme_from_name("WHITE"), 3);
    CHECK_EQ(bios_scheme_from_name("grey"), -1);
    CHECK_STR(bios_scheme_name(2), "blue");
    CHECK_STR(bios_scheme_name(4), "unknown");
}
