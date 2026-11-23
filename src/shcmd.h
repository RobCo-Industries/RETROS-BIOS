/*
 * The emergency shell's command language: the command table, the
 * tokeniser, number parsing, argument checks, the help text, the MEM
 * address rules, the hex dump line and the pager's line count. Pure code:
 * it builds and runs on the host in the unit tests. shell.c runs the
 * commands.
 *
 * Grammar: one command per line, at most SH_LINE_MAX characters. Words
 * are separated by spaces. Command and test names are case-insensitive,
 * paths are passed as typed (FAT names are case-insensitive anyway).
 * There are no abbreviations. Addresses and lengths are hex, with or
 * without 0x, 1 to 8 digits. Counts are decimal.
 */
#ifndef RETROS_SHCMD_H
#define RETROS_SHCMD_H

#include <stddef.h>
#include <stdint.h>

#include <robco/fs.h>

/*
 * The shell's prompt, and the longest command line without the NUL. The
 * prompt and a full line leave the cursor in the last of SH_COLS columns,
 * so a line never wraps and backspace never crosses a row. SH_LINE_MAX
 * is a plain number so that SH_STR() can put it in text.
 */
#define SH_PROMPT       "RETROS> "
#define SH_COLS         80
#define SH_LINE_MAX     71
#define SH_STR_(x)      #x
#define SH_STR(x)       SH_STR_(x)
_Static_assert(sizeof(SH_PROMPT) - 1 + SH_LINE_MAX + 1 == SH_COLS,
               "the prompt, a full line and the cursor fill one row");
/* Words a line may hold, the command included. */
#define SH_WORDS_MAX    4

/* Commands, in help order. */
enum {
    SH_HELP,
    SH_INFO,
    SH_POST,
    SH_VOL,
    SH_DIR,
    SH_TYPE,
    SH_MEM,
    SH_BOOT,
    SH_CONTINUE,
    SH_DIAG,
    SH_REBOOT,
    SH_CLS,
    SH_COUNT
};

/* Diagnostics, in list order. SH_DIAG_LIST is DIAG with no name. */
enum {
    SH_DIAG_MEM,
    SH_DIAG_SD,
    SH_DIAG_VIDEO,
    SH_DIAG_AUDIO,
    SH_DIAG_TIMER,
    SH_DIAG_RNG,
    SH_DIAG_RTC,
    SH_DIAG_INPUT,
    SH_DIAG_COUNT,
    SH_DIAG_ALL = SH_DIAG_COUNT,
    SH_DIAG_LIST
};

/* MEM limits. */
#define SH_MEM_DEFAULT  0x40u
#define SH_MEM_MAX      0x1000u
/* DIAG SD block count limits. */
#define SH_SD_DEFAULT   256u
#define SH_SD_MAX       2048u
/* DIR default folder. */
#define SH_DIR_DEFAULT  "/ROBCO"

/* A parsed line. Strings point into the line the parser was given. */
typedef struct {
    int id;             /* SH_* */
    const char *path;   /* DIR, TYPE, BOOT: the path, or NULL for the default */
    int topic;          /* HELP: SH_* of the argument, or -1 */
    int diag;           /* DIAG: SH_DIAG_* */
    uint32_t addr;      /* MEM, DIAG MEM */
    uint32_t len;       /* MEM, DIAG MEM; bytes */
    int has_range;      /* DIAG MEM: addr and len given */
    uint32_t count;     /* DIAG SD: blocks */
} sh_cmd_t;

#define SH_OK       0
#define SH_EMPTY    1   /* blank line: nothing to run */
#define SH_BAD      (-1)

/* Longest error line, with the NUL: fits 79 columns. */
#define SH_ERR_MAX  80u

/*
 * Parses `line` in place. Returns SH_OK with *cmd filled, SH_EMPTY for a
 * blank line, or SH_BAD with one line in err that says what is wrong and
 * ends with "Type HELP.". On SH_BAD, cmd->id is the command when the
 * name was known, else -1.
 */
int sh_parse(char *line, sh_cmd_t *cmd, char *err, size_t err_size);

/* Command number for a name, any case, or -1. */
int sh_lookup(const char *name);
/* Test number for a name, any case (ALL included), or -1. */
int sh_diag_lookup(const char *name);

/* Upper-case name of a command, such as "HELP". NULL past the end. */
const char *sh_name(int id);
/* Lower-case marker name of a command, such as "help". "unknown" past the end. */
const char *sh_marker_name(int id);
/* Upper-case name of a test, such as "TIMER"; "ALL" for SH_DIAG_ALL. NULL otherwise. */
const char *sh_diag_name(int diag);
/* Lower-case marker name of a test, such as "timer". */
const char *sh_diag_marker_name(int diag);
/* One line about a test, for DIAG with no name. */
const char *sh_diag_summary(int diag);

/* Help: line i of the command list, or NULL past the end. */
const char *sh_help_line(unsigned i);
/* Help: line i of the usage of command `id`, or NULL past the end. */
const char *sh_usage_line(int id, unsigned i);

/*
 * Hex with an optional 0x or 0X, 1 to 8 digits, nothing else. Returns 0
 * and sets *out, or -1.
 */
int sh_parse_hex(const char *s, uint32_t *out);
/* Decimal, 1 to 10 digits, from lo to hi. Returns 0 and sets *out, or -1. */
int sh_parse_dec(const char *s, uint32_t lo, uint32_t hi, uint32_t *out);

/* MEM address check results. */
#define SH_MEM_OK       0
#define SH_MEM_OUTSIDE  1   /* not inside ARM RAM, or wraps past 4 GiB */
#define SH_MEM_DEVICE   2   /* touches a peripheral range: reads may have side effects */

/*
 * Checks [addr, addr + len) for MEM. len is at least 1. RAM is
 * [ram_base, ram_base + ram_size). Device ranges: periph_base to
 * periph_base + 16 MiB, and 0x40000000 to 0x400FFFFF (local peripherals
 * of BCM2836 and BCM2837, checked on every board). A device range wins
 * over RAM.
 */
int sh_mem_check(uint32_t addr, uint32_t len, uint32_t ram_base, uint32_t ram_size,
                 uint32_t periph_base);

/*
 * One hex dump line: "AAAAAAAA: hh hh ... hh  cccccccccccccccc". n bytes
 * from bytes[], 1 to 16. A byte whose bit is clear in `valid` (bit i for
 * byte i) shows as "--" and "?". Non-printable bytes show as '.'. A
 * short line is padded so the text column stays aligned. Needs size of
 * at least SH_HEX_LINE.
 */
#define SH_HEX_LINE 80u
void sh_hex_line(char *buf, size_t size, uint32_t addr, const uint8_t *bytes, unsigned n,
                 uint32_t valid);

/* Byte as shown by TYPE: printable ASCII and '\n' and '\t' stay, others become '.'. CR is dropped (returns 0). */
char sh_type_char(uint8_t c);

/*
 * Pager. A screen of `rows` rows shows rows - 1 lines, then the more
 * prompt on the last one. A line of more than `cols` characters takes
 * several rows. rows below 2 or cols 0 turn paging off.
 */
typedef struct {
    unsigned rows, cols, used;
} sh_pager_t;

void sh_pager_init(sh_pager_t *p, unsigned rows, unsigned cols);
/* Rows a line of `len` characters takes: at least 1. */
unsigned sh_pager_rows(const sh_pager_t *p, size_t len);
/*
 * Counts a line of `len` characters about to be shown. Returns 1 when the
 * page is full and the caller must pause first; the count then restarts
 * with this line. Returns 0 otherwise.
 */
int sh_pager_add(sh_pager_t *p, size_t len);

/*
 * DIR's listing. next() reads one entry as fs_readdir() does: 1 with
 * *out set, 0 at the end of the folder, or a negative error. The first
 * `max` entries go into entries[], sorted by name in any case; *more
 * counts the rest. tmp is work space for one entry. Returns 0 at the end
 * of the folder, or the first error of next(): the entries read before
 * it stay, and the caller must report the error, never a full listing.
 */
typedef int (*sh_dir_next_fn)(void *ctx, fs_stat_t *out);
int sh_dir_read(sh_dir_next_fn next, void *ctx, fs_stat_t *entries, unsigned max,
                fs_stat_t *tmp, unsigned *n, unsigned *more);

/* DIR's totals: the folders, the files and their bytes. The 64-bit sum cannot wrap. */
typedef struct {
    unsigned files, dirs;
    uint64_t bytes;
} sh_dir_totals_t;

void sh_dir_count(const fs_stat_t *entries, unsigned n, sh_dir_totals_t *t);

/*
 * DIR's last line, such as "3 files, 1 folders, 5368709120 bytes": the
 * exact byte count in decimal, at most 20 digits. Cut to size - 1
 * characters. k_snprintf() has no 64-bit conversion, so this formats it.
 */
void sh_dir_count_line(char *buf, size_t size, const sh_dir_totals_t *t);

#endif
