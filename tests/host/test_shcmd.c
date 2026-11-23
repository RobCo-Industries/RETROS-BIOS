/* The shell's command language: every command, every bad form, numbers, MEM rules, the pager. */

#include <stdio.h>
#include <string.h>

#include "shcmd.h"
#include "rc_test.h"

static char line[128];
static char err[SH_ERR_MAX];
static sh_cmd_t c;

static int parse(const char *text)
{
    strncpy(line, text, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    err[0] = '\0';
    return sh_parse(line, &c, err, sizeof(err));
}

static int ends_with_help(void)
{
    size_t n = strlen(err);

    return n >= 10 && strcmp(err + n - 10, "Type HELP.") == 0;
}

#define CHECK_BAD(text, msg)                \
    do {                                    \
        CHECK_EQ(parse(text), SH_BAD);      \
        CHECK_STR(err, msg);                \
        CHECK(ends_with_help());            \
    } while (0)

TEST(blank_lines)
{
    CHECK_EQ(parse(""), SH_EMPTY);
    CHECK_EQ(parse("   "), SH_EMPTY);
    CHECK_EQ(parse("\t \t"), SH_EMPTY);
}

TEST(every_command_without_arguments)
{
    static const char *const names[SH_COUNT] = {
        "help", "INFO", "Post", "vol", "dir", NULL, NULL, "boot", "continue", "diag",
        "reboot", "cls",
    };
    int i;

    for (i = 0; i < SH_COUNT; i++) {
        if (names[i] == NULL) {
            continue;
        }
        CHECK_EQ(parse(names[i]), SH_OK);
        CHECK_EQ(c.id, i);
    }
    CHECK(c.path == NULL);
}

TEST(case_and_spaces)
{
    CHECK_EQ(parse("  hElP   dIaG  "), SH_OK);
    CHECK_EQ(c.id, SH_HELP);
    CHECK_EQ(c.topic, SH_DIAG);
}

TEST(no_abbreviations)
{
    CHECK_BAD("HEL", "Unknown command: HEL. Type HELP.");
    CHECK_BAD("CONT", "Unknown command: CONT. Type HELP.");
    CHECK_EQ(c.id, -1);
}

TEST(unknown_command_cut)
{
    CHECK_BAD("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "Unknown command: ABCDEFGHIJKLMNOPQRST. Type HELP.");
}

TEST(too_many_words)
{
    CHECK_BAD("MEM 1 2 3 4", "Too many words. Type HELP.");
    CHECK_EQ(c.id, SH_MEM);
}

TEST(long_line)
{
    char text[100];

    CHECK_EQ(SH_LINE_MAX, 71);
    CHECK_EQ(strlen(SH_PROMPT) + SH_LINE_MAX + 1, 80);
    memset(text, 'A', SH_LINE_MAX + 1);
    text[SH_LINE_MAX + 1] = '\0';
    CHECK_BAD(text, "Line is over 71 characters. Type HELP.");
    memcpy(text, "DIR /", 5);
    memset(text + 5, 'A', SH_LINE_MAX - 5);
    text[SH_LINE_MAX] = '\0';
    CHECK_EQ(parse(text), SH_OK);
    CHECK_EQ(strlen(c.path), 67);
    /* The help says the limit. */
    CHECK_STR(sh_usage_line(SH_HELP, 2), "  A command line holds at most 71 characters.");
}

TEST(no_argument_commands_refuse_arguments)
{
    CHECK_BAD("INFO x", "INFO takes no arguments. Type HELP.");
    CHECK_BAD("POST x", "POST takes no arguments. Type HELP.");
    CHECK_BAD("VOL x", "VOL takes no arguments. Type HELP.");
    CHECK_BAD("CONTINUE x", "CONTINUE takes no arguments. Type HELP.");
    CHECK_BAD("REBOOT now", "REBOOT takes no arguments. Type HELP.");
    CHECK_BAD("CLS 1", "CLS takes no arguments. Type HELP.");
    CHECK_EQ(c.id, SH_CLS);
}

TEST(help_forms)
{
    CHECK_EQ(parse("HELP"), SH_OK);
    CHECK_EQ(c.topic, -1);
    CHECK_EQ(parse("HELP mem"), SH_OK);
    CHECK_EQ(c.topic, SH_MEM);
    CHECK_BAD("HELP FOO", "No command named FOO. Type HELP.");
    CHECK_BAD("HELP MEM DIR", "Too many arguments for HELP. Type HELP.");
}

TEST(path_commands)
{
    CHECK_EQ(parse("DIR /ROBCO/SUB"), SH_OK);
    CHECK_STR(c.path, "/ROBCO/SUB");
    CHECK_EQ(parse("BOOT /ROBCO/MFBOOT.BAK"), SH_OK);
    CHECK_STR(c.path, "/ROBCO/MFBOOT.BAK");
    CHECK_EQ(parse("TYPE /ROBCO/BOOT.CFG"), SH_OK);
    CHECK_EQ(c.id, SH_TYPE);
    CHECK_STR(c.path, "/ROBCO/BOOT.CFG");
    CHECK_BAD("TYPE", "TYPE needs a path. Type HELP.");
    CHECK_BAD("TYPE a b", "Too many arguments for TYPE. Type HELP.");
    CHECK_BAD("DIR a b", "Too many arguments for DIR. Type HELP.");
    CHECK_BAD("BOOT a b", "Too many arguments for BOOT. Type HELP.");
}

TEST(mem_forms)
{
    CHECK_EQ(parse("MEM 400000"), SH_OK);
    CHECK_EQ(c.addr, 0x400000);
    CHECK_EQ(c.len, SH_MEM_DEFAULT);
    CHECK_EQ(parse("mem 0x8000 0x1000"), SH_OK);
    CHECK_EQ(c.addr, 0x8000);
    CHECK_EQ(c.len, 0x1000);
    CHECK_EQ(parse("MEM 0XFFFFFFFF 1"), SH_OK);
    CHECK_EQ(c.addr, 0xffffffffu);
    CHECK_EQ(c.len, 1);
    CHECK_BAD("MEM", "MEM needs an address. Type HELP.");
    CHECK_BAD("MEM 1 2 3", "Too many arguments for MEM. Type HELP.");
    CHECK_BAD("MEM xyz", "Bad address: xyz. Type HELP.");
    CHECK_BAD("MEM 0x", "Bad address: 0x. Type HELP.");
    CHECK_BAD("MEM 123456789", "Bad address: 123456789. Type HELP.");
    CHECK_BAD("MEM 0 -1", "Bad length: -1. Type HELP.");
    CHECK_BAD("MEM 0 0", "Length must be 1 to 1000 (hex). Type HELP.");
    CHECK_BAD("MEM 0 1001", "Length must be 1 to 1000 (hex). Type HELP.");
}

TEST(diag_forms)
{
    int i;

    CHECK_EQ(parse("DIAG"), SH_OK);
    CHECK_EQ(c.diag, SH_DIAG_LIST);
    for (i = 0; i <= SH_DIAG_ALL; i++) {
        char text[32];

        snprintf(text, sizeof(text), "diag %s", sh_diag_name(i));
        CHECK_EQ(parse(text), SH_OK);
        CHECK_EQ(c.diag, i);
        CHECK_EQ(c.has_range, 0);
    }
    CHECK_EQ(parse("DIAG SD"), SH_OK);
    CHECK_EQ(c.count, SH_SD_DEFAULT);
    CHECK_EQ(parse("DIAG SD 2048"), SH_OK);
    CHECK_EQ(c.count, 2048);
    CHECK_EQ(parse("DIAG MEM 400000 1000"), SH_OK);
    CHECK_EQ(c.has_range, 1);
    CHECK_EQ(c.addr, 0x400000);
    CHECK_EQ(c.len, 0x1000);
    CHECK_BAD("DIAG FOO", "Unknown test: FOO. Type HELP.");
    CHECK_BAD("DIAG SD 0", "Bad count: 0, use 1 to 2048. Type HELP.");
    CHECK_BAD("DIAG SD 2049", "Bad count: 2049, use 1 to 2048. Type HELP.");
    CHECK_BAD("DIAG SD 0x10", "Bad count: 0x10, use 1 to 2048. Type HELP.");
    CHECK_BAD("DIAG SD 1 2", "Too many arguments for DIAG SD. Type HELP.");
    CHECK_BAD("DIAG MEM 1 2 3", "Too many words. Type HELP.");
    CHECK_BAD("DIAG MEM 400000", "DIAG MEM takes an address and a length, or neither. Type HELP.");
    CHECK_BAD("DIAG MEM 400002 100",
              "Address and length must be multiples of 4, length not 0. Type HELP.");
    CHECK_BAD("DIAG MEM 400000 3",
              "Address and length must be multiples of 4, length not 0. Type HELP.");
    CHECK_BAD("DIAG MEM 400000 0",
              "Address and length must be multiples of 4, length not 0. Type HELP.");
    CHECK_BAD("DIAG MEM q 4", "Bad address: q. Type HELP.");
    CHECK_BAD("DIAG MEM 0 q", "Bad length: q. Type HELP.");
    CHECK_BAD("DIAG RNG 1", "Too many arguments for DIAG RNG. Type HELP.");
}

TEST(numbers)
{
    uint32_t v = 7;

    CHECK_EQ(sh_parse_hex("0", &v), 0);
    CHECK_EQ(v, 0);
    CHECK_EQ(sh_parse_hex("aBcD", &v), 0);
    CHECK_EQ(v, 0xabcd);
    CHECK_EQ(sh_parse_hex("ffffffff", &v), 0);
    CHECK_EQ(v, 0xffffffffu);
    CHECK_EQ(sh_parse_hex("100000000", &v), -1);
    CHECK_EQ(sh_parse_hex("", &v), -1);
    CHECK_EQ(sh_parse_hex("0x", &v), -1);
    CHECK_EQ(sh_parse_hex("g", &v), -1);
    CHECK_EQ(sh_parse_hex("1 ", &v), -1);
    CHECK_EQ(v, 0xffffffffu);
    CHECK_EQ(sh_parse_dec("4294967295", 0, 0xffffffffu, &v), 0);
    CHECK_EQ(v, 0xffffffffu);
    CHECK_EQ(sh_parse_dec("4294967296", 0, 0xffffffffu, &v), -1);
    CHECK_EQ(sh_parse_dec("00000000001", 0, 10, &v), -1);
    CHECK_EQ(sh_parse_dec("0000000001", 0, 10, &v), 0);
    CHECK_EQ(v, 1);
    CHECK_EQ(sh_parse_dec("", 0, 10, &v), -1);
    CHECK_EQ(sh_parse_dec("+1", 0, 10, &v), -1);
    CHECK_EQ(sh_parse_dec("11", 0, 10, &v), -1);
    CHECK_EQ(sh_parse_dec("1", 2, 10, &v), -1);
}

TEST(names_and_help)
{
    unsigned i;
    int id;

    for (id = 0; id < SH_COUNT; id++) {
        CHECK_EQ(sh_lookup(sh_name(id)), id);
        CHECK(sh_usage_line(id, 0) != NULL);
        CHECK(strncmp(sh_usage_line(id, 0), sh_name(id), strlen(sh_name(id))) == 0);
        for (i = 0; sh_usage_line(id, i) != NULL; i++) {
            CHECK(strlen(sh_usage_line(id, i)) <= 79);
        }
    }
    CHECK(sh_name(SH_COUNT) == NULL);
    CHECK_STR(sh_marker_name(SH_CONTINUE), "continue");
    CHECK_STR(sh_marker_name(-1), "unknown");
    CHECK(sh_usage_line(SH_COUNT, 0) == NULL);
    for (i = 0; sh_help_line(i) != NULL; i++) {
        CHECK(strlen(sh_help_line(i)) <= 79);
    }
    CHECK_EQ(i, SH_COUNT + 2);
    CHECK_STR(sh_diag_marker_name(SH_DIAG_TIMER), "timer");
    CHECK_STR(sh_diag_name(SH_DIAG_ALL), "ALL");
    CHECK(sh_diag_name(SH_DIAG_LIST) == NULL);
    CHECK_EQ(sh_diag_lookup("rtc"), SH_DIAG_RTC);
    CHECK_EQ(sh_diag_lookup("rt"), -1);
}

TEST(mem_check)
{
    const uint32_t ram = 0x3c000000u, periph = 0x3f000000u;

    CHECK_EQ(sh_mem_check(0, 0x40, 0, ram, periph), SH_MEM_OK);
    CHECK_EQ(sh_mem_check(ram - 1, 1, 0, ram, periph), SH_MEM_OK);
    CHECK_EQ(sh_mem_check(ram - 1, 2, 0, ram, periph), SH_MEM_OUTSIDE);
    CHECK_EQ(sh_mem_check(ram, 1, 0, ram, periph), SH_MEM_OUTSIDE);
    CHECK_EQ(sh_mem_check(periph, 4, 0, ram, periph), SH_MEM_DEVICE);
    CHECK_EQ(sh_mem_check(periph - 4, 8, 0, ram, periph), SH_MEM_DEVICE);
    CHECK_EQ(sh_mem_check(periph + 0xfffffcu, 4, 0, ram, periph), SH_MEM_DEVICE);
    CHECK_EQ(sh_mem_check(0x40100000u, 4, 0, ram, periph), SH_MEM_OUTSIDE);
    CHECK_EQ(sh_mem_check(0x40000000u, 4, 0, ram, periph), SH_MEM_DEVICE);
    CHECK_EQ(sh_mem_check(0x400ffffcu, 4, 0, ram, periph), SH_MEM_DEVICE);
    CHECK_EQ(sh_mem_check(0xfffffff0u, 0x10, 0, ram, periph), SH_MEM_OUTSIDE);
    CHECK_EQ(sh_mem_check(0xfffffff0u, 0x11, 0, 0xffffffffu, 0), SH_MEM_OUTSIDE);
    /* BCM2835: peripherals at 0x20000000, RAM below. */
    CHECK_EQ(sh_mem_check(0x20000000u, 4, 0, 0x1c000000u, 0x20000000u), SH_MEM_DEVICE);
    CHECK_EQ(sh_mem_check(0x1bfffff0u, 0x10, 0, 0x1c000000u, 0x20000000u), SH_MEM_OK);
    /* A device range wins over RAM that covers it. */
    CHECK_EQ(sh_mem_check(0x20000000u, 4, 0, 0x40000000u, 0x20000000u), SH_MEM_DEVICE);
}

TEST(hex_line)
{
    char buf[SH_HEX_LINE];
    uint8_t b[16];
    unsigned i;

    for (i = 0; i < 16; i++) {
        b[i] = (uint8_t)(0x41 + i);
    }
    b[3] = 0x00;
    b[4] = 0x7f;
    b[5] = 0xff;
    sh_hex_line(buf, sizeof(buf), 0x00400010u, b, 16, 0xffffu);
    CHECK_STR(buf, "00400010: 41 42 43 00 7f ff 47 48 49 4a 4b 4c 4d 4e 4f 50  ABC...GHIJKLMNOP");
    CHECK(strlen(buf) <= 79);
    sh_hex_line(buf, sizeof(buf), 0xfffffff0u, b, 2, 0x1u);
    CHECK_STR(buf, "fffffff0: 41 --                                            A?");
}

TEST(type_chars)
{
    CHECK_EQ(sh_type_char('A'), 'A');
    CHECK_EQ(sh_type_char('\n'), '\n');
    CHECK_EQ(sh_type_char('\t'), '\t');
    CHECK_EQ(sh_type_char('\r'), 0);
    CHECK_EQ(sh_type_char(0), '.');
    CHECK_EQ(sh_type_char(0x7f), '.');
    CHECK_EQ(sh_type_char(0xc3), '.');
}

TEST(pager)
{
    sh_pager_t p;
    unsigned i;

    sh_pager_init(&p, 30, 80);
    for (i = 0; i < 29; i++) {
        CHECK_EQ(sh_pager_add(&p, 10), 0);
    }
    CHECK_EQ(sh_pager_add(&p, 10), 1);
    CHECK_EQ(p.used, 1);
    /* Wrapped lines count their rows. */
    sh_pager_init(&p, 30, 80);
    CHECK_EQ(sh_pager_rows(&p, 0), 1);
    CHECK_EQ(sh_pager_rows(&p, 80), 1);
    CHECK_EQ(sh_pager_rows(&p, 81), 2);
    for (i = 0; i < 14; i++) {
        CHECK_EQ(sh_pager_add(&p, 160), 0);
    }
    CHECK_EQ(p.used, 28);
    CHECK_EQ(sh_pager_add(&p, 81), 1);
    CHECK_EQ(p.used, 2);
    /* A line longer than a page never loops. */
    sh_pager_init(&p, 4, 10);
    CHECK_EQ(sh_pager_add(&p, 100), 0);
    CHECK_EQ(sh_pager_add(&p, 1), 1);
    /* Off. */
    sh_pager_init(&p, 1, 80);
    for (i = 0; i < 100; i++) {
        CHECK_EQ(sh_pager_add(&p, 10), 0);
    }
    sh_pager_init(&p, 30, 0);
    CHECK_EQ(sh_pager_add(&p, 10), 0);
}

/* A fake folder: names[] in order, then `end` (0 for the end, or an error). */
typedef struct {
    const char *const *names;
    unsigned count, pos;
    int end;
} fake_dir_t;

static int fake_next(void *ctx, fs_stat_t *out)
{
    fake_dir_t *d = ctx;

    if (d->pos == d->count) {
        return d->end;
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "%s", d->names[d->pos++]);
    return 1;
}

static fs_stat_t dir_entries[4];
static fs_stat_t dir_tmp;

TEST(dir_read_sorts_and_counts)
{
    static const char *const names[] = { "b.txt", "A.TXT", "SUB", "c", "BIG.TXT", "a2" };
    fake_dir_t d = { names, 6, 0, 0 };
    unsigned n = 9, more = 9;

    CHECK_EQ(sh_dir_read(fake_next, &d, dir_entries, 4, &dir_tmp, &n, &more), 0);
    CHECK_EQ(n, 4);
    CHECK_EQ(more, 2);
    /* The first four read, sorted in any case: A.TXT, b.txt, c, SUB. */
    CHECK_STR(dir_entries[0].name, "A.TXT");
    CHECK_STR(dir_entries[1].name, "b.txt");
    CHECK_STR(dir_entries[2].name, "c");
    CHECK_STR(dir_entries[3].name, "SUB");
}

TEST(dir_read_empty_folder)
{
    fake_dir_t d = { NULL, 0, 0, 0 };
    unsigned n = 9, more = 9;

    CHECK_EQ(sh_dir_read(fake_next, &d, dir_entries, 4, &dir_tmp, &n, &more), 0);
    CHECK_EQ(n, 0);
    CHECK_EQ(more, 0);
}

/* An error partway through never looks like the end of the folder. */
TEST(dir_read_error_partway)
{
    static const char *const names[] = { "Z", "Y" };
    static const char *const six[] = { "1", "2", "3", "4", "5", "6" };
    fake_dir_t d = { names, 2, 0, FS_ERR_CORRUPT };
    unsigned n = 0, more = 0;

    CHECK_EQ(sh_dir_read(fake_next, &d, dir_entries, 4, &dir_tmp, &n, &more), FS_ERR_CORRUPT);
    CHECK_EQ(n, 2);
    CHECK_STR(dir_entries[0].name, "Y");
    /* At once, and past the list size. */
    d.count = 0;
    d.pos = 0;
    d.end = FS_ERR_IO;
    CHECK_EQ(sh_dir_read(fake_next, &d, dir_entries, 4, &dir_tmp, &n, &more), FS_ERR_IO);
    CHECK_EQ(n, 0);
    d.names = six;
    d.count = 6;
    d.pos = 0;
    d.end = FS_ERR_TIMEOUT;
    CHECK_EQ(sh_dir_read(fake_next, &d, dir_entries, 4, &dir_tmp, &n, &more), FS_ERR_TIMEOUT);
    CHECK_EQ(n, 4);
    CHECK_EQ(more, 2);
}

static fs_stat_t big_entries[128];

static void count_line(unsigned files, unsigned dirs, uint32_t size, char *line, size_t len)
{
    sh_dir_totals_t t;
    unsigned i;

    memset(big_entries, 0, sizeof(big_entries));
    for (i = 0; i < files + dirs; i++) {
        big_entries[i].is_dir = i >= files;
        big_entries[i].size = i >= files ? 0 : size;
    }
    sh_dir_count(big_entries, files + dirs, &t);
    CHECK_EQ(t.files, files);
    CHECK_EQ(t.dirs, dirs);
    CHECK(t.bytes == (uint64_t)files * size);
    sh_dir_count_line(line, len, &t);
}

/* The total is 64-bit: 128 files of almost 4 GiB do not wrap. */
TEST(dir_count_line_no_wrap)
{
    char line[80];
    sh_dir_totals_t t = { 0, 0, 0 };

    sh_dir_count(big_entries, 0, &t);
    sh_dir_count_line(line, sizeof(line), &t);
    CHECK_STR(line, "0 files, 0 folders, 0 bytes");
    count_line(1, 0, 20, line, sizeof(line));
    CHECK_STR(line, "1 files, 0 folders, 20 bytes");
    /* Below 2^32. */
    count_line(3, 2, 1000000000u, line, sizeof(line));
    CHECK_STR(line, "3 files, 2 folders, 3000000000 bytes");
    /* 2^32 - 1, then just past 2^32. */
    count_line(1, 0, 4294967295u, line, sizeof(line));
    CHECK_STR(line, "1 files, 0 folders, 4294967295 bytes");
    count_line(2, 0, 2147483648u, line, sizeof(line));
    CHECK_STR(line, "2 files, 0 folders, 4294967296 bytes");
    count_line(2, 1, 4294967295u, line, sizeof(line));
    CHECK_STR(line, "2 files, 1 folders, 8589934590 bytes");
    /* The largest listing: 128 files of 4294967295 bytes. */
    count_line(128, 0, 4294967295u, line, sizeof(line));
    CHECK_STR(line, "128 files, 0 folders, 549755813760 bytes");
    /* The full 64-bit range formats too. */
    t.bytes = UINT64_MAX;
    t.files = 4294967295u;
    t.dirs = 4294967295u;
    sh_dir_count_line(line, sizeof(line), &t);
    CHECK_STR(line, "4294967295 files, 4294967295 folders, 18446744073709551615 bytes");
}
