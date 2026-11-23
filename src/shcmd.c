#include "shcmd.h"

#include <robco/input.h>
#include <robco/klibc.h>

typedef struct {
    const char *name;       /* upper case */
    const char *marker;     /* lower case */
    const char *summary;    /* the help list line */
    const char *const *usage;
} sh_info_t;

static const char *const u_help[] = {
    "HELP [command]",
    "  Lists the commands. With a command name, shows its usage.",
    NULL,
};
static const char *const u_info[] = {
    "INFO",
    "  BIOS version, board, SoC, memory, boot volume, settings, last error",
    "  and time since power-on.",
    NULL,
};
static const char *const u_post[] = {
    "POST",
    "  Runs the power-on self test again and shows its table.",
    NULL,
};
static const char *const u_vol[] = {
    "VOL",
    "  The partition table of the SD card and the chosen boot volume.",
    NULL,
};
static const char *const u_dir[] = {
    "DIR [path]",
    "  Lists a folder of the boot volume, sorted by name. Default /ROBCO.",
    "  A folder shows <DIR> in place of its size.",
    NULL,
};
static const char *const u_type[] = {
    "TYPE <path>",
    "  Prints a text file of the boot volume. Shows at most 16 KiB.",
    "  Bytes that are not printable show as a dot.",
    NULL,
};
static const char *const u_mem[] = {
    "MEM <addr> [len]",
    "  Hex dump of RAM. addr and len are hex, with or without 0x.",
    "  len is 1 to 1000 (hex), default 40. Device addresses are refused.",
    NULL,
};
static const char *const u_boot[] = {
    "BOOT [path]",
    "  Loads and starts a second stage. No path: /ROBCO/MFBOOT.SYS, then",
    "  /ROBCO/MFBOOT.BAK if it fails. With a path: that file, no fallback.",
    NULL,
};
static const char *const u_continue[] = {
    "CONTINUE",
    "  Goes on with the normal boot. Only when a key opened the shell.",
    NULL,
};
static const char *const u_diag[] = {
    "DIAG [test]",
    "  No test: lists the tests. ALL runs every test but INPUT.",
    "  DIAG MEM [addr len]: addr and len hex, multiples of 4.",
    "  DIAG SD [count]: count decimal, 1 to 2048 blocks, default 256.",
    NULL,
};
static const char *const u_reboot[] = {
    "REBOOT",
    "  Restarts the board through the watchdog.",
    NULL,
};
static const char *const u_cls[] = {
    "CLS",
    "  Clears the screen.",
    NULL,
};

static const sh_info_t cmds[SH_COUNT] = {
    [SH_HELP]     = { "HELP", "help", "HELP [command]    This list, or the usage of one command", u_help },
    [SH_INFO]     = { "INFO", "info", "INFO              Version, board, memory, settings, uptime", u_info },
    [SH_POST]     = { "POST", "post", "POST              Runs the self test again", u_post },
    [SH_VOL]      = { "VOL", "vol", "VOL               Partition table and boot volume", u_vol },
    [SH_DIR]      = { "DIR", "dir", "DIR [path]        Lists a folder, default /ROBCO", u_dir },
    [SH_TYPE]     = { "TYPE", "type", "TYPE <path>       Prints a text file", u_type },
    [SH_MEM]      = { "MEM", "mem", "MEM <addr> [len]  Hex dump of RAM", u_mem },
    [SH_BOOT]     = { "BOOT", "boot", "BOOT [path]       Loads and starts a second stage", u_boot },
    [SH_CONTINUE] = { "CONTINUE", "continue", "CONTINUE          Goes on with the normal boot", u_continue },
    [SH_DIAG]     = { "DIAG", "diag", "DIAG [test]       Diagnostics, see HELP DIAG", u_diag },
    [SH_REBOOT]   = { "REBOOT", "reboot", "REBOOT            Restarts the board", u_reboot },
    [SH_CLS]      = { "CLS", "cls", "CLS               Clears the screen", u_cls },
};

typedef struct {
    const char *name;
    const char *marker;
    const char *summary;
} sh_diag_info_t;

static const sh_diag_info_t diags[SH_DIAG_COUNT + 1] = {
    [SH_DIAG_MEM]   = { "MEM", "mem", "MEM    Long memory test, four patterns" },
    [SH_DIAG_SD]    = { "SD", "sd", "SD     Reads blocks of the boot volume, speed" },
    [SH_DIAG_VIDEO] = { "VIDEO", "video", "VIDEO  Test pattern for 3 seconds" },
    [SH_DIAG_AUDIO] = { "AUDIO", "audio", "AUDIO  Rising scale and a short sound" },
    [SH_DIAG_TIMER] = { "TIMER", "timer", "TIMER  System timer against the tick, 1 second" },
    [SH_DIAG_RNG]   = { "RNG", "rng", "RNG    1024 words: repeats and bit balance" },
    [SH_DIAG_RTC]   = { "RTC", "rtc", "RTC    Reads the clock chip" },
    [SH_DIAG_INPUT] = { "INPUT", "input", "INPUT  Shows key codes, 10 seconds or Q" },
    [SH_DIAG_ALL]   = { "ALL", "all", "ALL    Every test but INPUT" },
};

static char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static int same_name(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (upper(*a) != upper(*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

int sh_lookup(const char *name)
{
    int i;

    for (i = 0; i < SH_COUNT; i++) {
        if (same_name(name, cmds[i].name)) {
            return i;
        }
    }
    return -1;
}

int sh_diag_lookup(const char *name)
{
    int i;

    for (i = 0; i <= SH_DIAG_ALL; i++) {
        if (same_name(name, diags[i].name)) {
            return i;
        }
    }
    return -1;
}

const char *sh_name(int id)
{
    return id >= 0 && id < SH_COUNT ? cmds[id].name : NULL;
}

const char *sh_marker_name(int id)
{
    return id >= 0 && id < SH_COUNT ? cmds[id].marker : "unknown";
}

const char *sh_diag_name(int diag)
{
    return diag >= 0 && diag <= SH_DIAG_ALL ? diags[diag].name : NULL;
}

const char *sh_diag_marker_name(int diag)
{
    return diag >= 0 && diag <= SH_DIAG_ALL ? diags[diag].marker : "unknown";
}

const char *sh_diag_summary(int diag)
{
    return diag >= 0 && diag <= SH_DIAG_ALL ? diags[diag].summary : NULL;
}

static const char *const help_head[] = {
    "Commands. Names are not case-sensitive. HELP <command> for usage.",
};
static const char *const help_tail[] = {
    "Numbers: addresses and lengths hex, counts decimal.",
};

const char *sh_help_line(unsigned i)
{
    const unsigned head = sizeof(help_head) / sizeof(help_head[0]);
    const unsigned tail = sizeof(help_tail) / sizeof(help_tail[0]);

    if (i < head) {
        return help_head[i];
    }
    i -= head;
    if (i < SH_COUNT) {
        return cmds[i].summary;
    }
    i -= SH_COUNT;
    return i < tail ? help_tail[i] : NULL;
}

const char *sh_usage_line(int id, unsigned i)
{
    const char *const *u;
    unsigned n;

    if (id < 0 || id >= SH_COUNT) {
        return NULL;
    }
    u = cmds[id].usage;
    for (n = 0; u[n] != NULL; n++) {
        if (n == i) {
            return u[n];
        }
    }
    return NULL;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = upper(c);
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

int sh_parse_hex(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    unsigned n = 0;
    int d;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    for (; *s != '\0'; s++) {
        d = hex_digit(*s);
        if (d < 0 || ++n > 8) {
            return -1;
        }
        v = (v << 4) | (uint32_t)d;
    }
    if (n == 0) {
        return -1;
    }
    *out = v;
    return 0;
}

int sh_parse_dec(const char *s, uint32_t lo, uint32_t hi, uint32_t *out)
{
    uint32_t v = 0;
    unsigned n = 0;

    for (; *s != '\0'; s++) {
        uint32_t d;

        if (*s < '0' || *s > '9' || ++n > 10) {
            return -1;
        }
        d = (uint32_t)(*s - '0');
        if (v > (0xffffffffu - d) / 10u) {
            return -1;
        }
        v = v * 10u + d;
    }
    if (n == 0 || v < lo || v > hi) {
        return -1;
    }
    *out = v;
    return 0;
}

/* Writes "<what>. Type HELP." with a word cut to 20 characters. */
static int bad(char *err, size_t size, const char *fmt, const char *word)
{
    char msg[SH_ERR_MAX];

    k_snprintf(msg, sizeof(msg), fmt, word != NULL ? word : "");
    k_snprintf(err, size, "%s. Type HELP.", msg);
    return SH_BAD;
}

static int parse_diag(int argc, char **argv, sh_cmd_t *c, char *err, size_t size)
{
    if (argc == 1) {
        c->diag = SH_DIAG_LIST;
        return SH_OK;
    }
    c->diag = sh_diag_lookup(argv[1]);
    if (c->diag < 0) {
        return bad(err, size, "Unknown test: %.20s", argv[1]);
    }
    if (c->diag == SH_DIAG_MEM) {
        if (argc == 2) {
            return SH_OK;
        }
        if (argc != 4) {
            return bad(err, size, "DIAG MEM takes an address and a length, or neither", NULL);
        }
        if (sh_parse_hex(argv[2], &c->addr) != 0) {
            return bad(err, size, "Bad address: %.20s", argv[2]);
        }
        if (sh_parse_hex(argv[3], &c->len) != 0) {
            return bad(err, size, "Bad length: %.20s", argv[3]);
        }
        if (c->len == 0 || (c->addr & 3u) != 0 || (c->len & 3u) != 0) {
            return bad(err, size, "Address and length must be multiples of 4, length not 0",
                       NULL);
        }
        c->has_range = 1;
        return SH_OK;
    }
    if (c->diag == SH_DIAG_SD) {
        if (argc == 2) {
            return SH_OK;
        }
        if (argc != 3) {
            return bad(err, size, "Too many arguments for DIAG SD", NULL);
        }
        if (sh_parse_dec(argv[2], 1, SH_SD_MAX, &c->count) != 0) {
            return bad(err, size, "Bad count: %.20s, use 1 to 2048", argv[2]);
        }
        return SH_OK;
    }
    if (argc != 2) {
        return bad(err, size, "Too many arguments for DIAG %s", sh_diag_name(c->diag));
    }
    return SH_OK;
}

int sh_parse(char *line, sh_cmd_t *c, char *err, size_t size)
{
    char *argv[SH_WORDS_MAX + 1];
    int argc;
    int id;

    k_memset(c, 0, sizeof(*c));
    c->id = -1;
    c->topic = -1;
    c->count = SH_SD_DEFAULT;
    c->len = SH_MEM_DEFAULT;
    if (k_strlen(line) > SH_LINE_MAX) {
        return bad(err, size, "Line is over 78 characters", NULL);
    }
    argc = input_line_split(line, argv, SH_WORDS_MAX);
    if (argc == 0) {
        return SH_EMPTY;
    }
    if (argc < 0) {
        id = sh_lookup(line);
        c->id = id;
        return bad(err, size, "Too many words", NULL);
    }
    id = sh_lookup(argv[0]);
    c->id = id;
    if (id < 0) {
        return bad(err, size, "Unknown command: %.20s", argv[0]);
    }
    switch (id) {
    case SH_HELP:
        if (argc > 2) {
            return bad(err, size, "Too many arguments for %s", cmds[id].name);
        }
        if (argc == 2 && (c->topic = sh_lookup(argv[1])) < 0) {
            return bad(err, size, "No command named %.20s", argv[1]);
        }
        return SH_OK;
    case SH_DIR:
    case SH_BOOT:
        if (argc > 2) {
            return bad(err, size, "Too many arguments for %s", cmds[id].name);
        }
        c->path = argc == 2 ? argv[1] : NULL;
        return SH_OK;
    case SH_TYPE:
        if (argc != 2) {
            return bad(err, size, argc < 2 ? "TYPE needs a path" : "Too many arguments for TYPE",
                       NULL);
        }
        c->path = argv[1];
        return SH_OK;
    case SH_MEM:
        if (argc < 2 || argc > 3) {
            return bad(err, size, argc < 2 ? "MEM needs an address" : "Too many arguments for MEM",
                       NULL);
        }
        if (sh_parse_hex(argv[1], &c->addr) != 0) {
            return bad(err, size, "Bad address: %.20s", argv[1]);
        }
        if (argc == 3) {
            if (sh_parse_hex(argv[2], &c->len) != 0) {
                return bad(err, size, "Bad length: %.20s", argv[2]);
            }
            if (c->len == 0 || c->len > SH_MEM_MAX) {
                return bad(err, size, "Length must be 1 to 1000 (hex)", NULL);
            }
        }
        return SH_OK;
    case SH_DIAG:
        return parse_diag(argc, argv, c, err, size);
    default:
        if (argc != 1) {
            return bad(err, size, "%s takes no arguments", cmds[id].name);
        }
        return SH_OK;
    }
}

static int overlaps(uint32_t a, uint32_t alen, uint32_t b, uint32_t blen)
{
    /* Both ranges are inside 4 GiB here: compare in 64 bits. */
    uint64_t ae = (uint64_t)a + alen;
    uint64_t be = (uint64_t)b + blen;

    return a < be && b < ae;
}

int sh_mem_check(uint32_t addr, uint32_t len, uint32_t ram_base, uint32_t ram_size,
                 uint32_t periph_base)
{
    uint64_t end = (uint64_t)addr + len;

    if (len == 0 || end > 0x100000000ull) {
        return SH_MEM_OUTSIDE;
    }
    if ((periph_base != 0 && overlaps(addr, len, periph_base, 0x01000000u)) ||
        overlaps(addr, len, 0x40000000u, 0x00100000u)) {
        return SH_MEM_DEVICE;
    }
    if (addr < ram_base || end > (uint64_t)ram_base + ram_size) {
        return SH_MEM_OUTSIDE;
    }
    return SH_MEM_OK;
}

void sh_hex_line(char *buf, size_t size, uint32_t addr, const uint8_t *bytes, unsigned n,
                 uint32_t valid)
{
    static const char hex[] = "0123456789abcdef";
    char line[SH_HEX_LINE];
    unsigned i;
    size_t p;

    if (n > 16) {
        n = 16;
    }
    k_snprintf(line, sizeof(line), "%08x:", (unsigned)addr);
    p = 9;
    for (i = 0; i < 16; i++) {
        line[p++] = ' ';
        if (i >= n) {
            line[p++] = ' ';
            line[p++] = ' ';
        } else if ((valid & (1u << i)) == 0) {
            line[p++] = '-';
            line[p++] = '-';
        } else {
            line[p++] = hex[bytes[i] >> 4];
            line[p++] = hex[bytes[i] & 15u];
        }
    }
    line[p++] = ' ';
    line[p++] = ' ';
    for (i = 0; i < n; i++) {
        if ((valid & (1u << i)) == 0) {
            line[p++] = '?';
        } else {
            line[p++] = (bytes[i] >= 0x20 && bytes[i] < 0x7f) ? (char)bytes[i] : '.';
        }
    }
    line[p] = '\0';
    k_snprintf(buf, size, "%s", line);
}

char sh_type_char(uint8_t c)
{
    if (c == '\r') {
        return 0;
    }
    if (c == '\n' || c == '\t' || (c >= 0x20 && c < 0x7f)) {
        return (char)c;
    }
    return '.';
}

void sh_pager_init(sh_pager_t *p, unsigned rows, unsigned cols)
{
    p->rows = rows;
    p->cols = cols;
    p->used = 0;
}

unsigned sh_pager_rows(const sh_pager_t *p, size_t len)
{
    if (p->cols == 0 || len == 0) {
        return 1;
    }
    return (unsigned)((len + p->cols - 1) / p->cols);
}

int sh_pager_add(sh_pager_t *p, size_t len)
{
    unsigned n;

    if (p->rows < 2 || p->cols == 0) {
        return 0;
    }
    n = sh_pager_rows(p, len);
    if (p->used + n > p->rows - 1 && p->used != 0) {
        p->used = n;
        return 1;
    }
    p->used += n;
    return 0;
}
