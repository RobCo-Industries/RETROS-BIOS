/*
 * The emergency shell. It reads lines from the PL011 console through the
 * console input backend, and shows its output on the screen and the
 * PL011, as all BIOS text. shcmd.c parses each line; this file runs the
 * commands. The maintenance port keeps answering while the shell waits.
 */

#include <stdint.h>

#include <robco/audio.h>
#include <robco/blk.h>
#include <robco/board.h>
#include <robco/console.h>
#include <robco/fs.h>
#include <robco/input.h>
#include <robco/klibc.h>
#include <robco/loader.h>
#include <robco/mmio.h>
#include <robco/part.h>
#include <robco/rimg.h>
#include <robco/power.h>
#include <robco/sd.h>
#include <robco/timer.h>
#include <robco/uart.h>

#include "bios.h"
#include "boot.h"
#include "bootlines.h"
#include "bootvol.h"
#include "diag.h"
#include "shcmd.h"

#define MORE        "-- more (any key, Q to stop) --"
#define HISTORY     4
#define TYPE_MAX    16384u
#define DIR_MAX     128u
/* Rows when there is no screen: a common serial terminal. */
#define SERIAL_ROWS 24u

static int paging = 1;
static int stopped;         /* Q at a more prompt: the rest of the output is dropped */
static sh_pager_t pager;
static char history[HISTORY][SH_LINE_MAX + 1];
static unsigned history_len;
static int from_keys;

void shell_set_paging(int on)
{
    paging = on;
}

static void echo(char c)
{
    if (bios.fb_ok) {
        console_putc(c);
    }
    uart_putc(c);
}

/* Waits for a console key: a CHAR, or a KEY press. 1 with *ev, or 0 after ms. */
int shell_key(input_event_t *ev, uint32_t ms)
{
    uint64_t end = timer_now_us() + (uint64_t)ms * 1000u;
    uint64_t now;

    while ((now = timer_now_us()) < end) {
        uint32_t left = (uint32_t)((end - now + 999u) / 1000u);

        audio_poll();
        if (input_wait(ev, left > 50u ? 50u : left) == 0) {
            continue;
        }
        if (ev->source != INPUT_SRC_SERIAL) {
            continue;
        }
        if (ev->type == INPUT_EV_CHAR || (ev->type == INPUT_EV_KEY && ev->value == INPUT_PRESS)) {
            return 1;
        }
    }
    return 0;
}

/* A console key already waiting, without a wait. Script lines are served too. */
int shell_key_pending(void)
{
    input_event_t ev;

    while (input_poll(&ev)) {
        if (ev.source == INPUT_SRC_SERIAL &&
            (ev.type == INPUT_EV_CHAR || (ev.type == INPUT_EV_KEY && ev.value == INPUT_PRESS))) {
            return 1;
        }
    }
    return 0;
}

static void pager_start(void)
{
    unsigned rows = bios.fb_ok ? console_rows() : SERIAL_ROWS;
    unsigned cols = bios.fb_ok ? console_cols() : 80u;

    sh_pager_init(&pager, paging ? rows : 0, cols);
    stopped = 0;
}

/* One line of command output, through the pager. Returns -1 once Q stopped the output. */
int shell_say(const char *line)
{
    input_event_t ev;

    if (stopped) {
        return -1;
    }
    if (sh_pager_add(&pager, k_strlen(line))) {
        bios_put(MORE);
        bios.prompt_open = 1;
        (void)shell_key(&ev, INPUT_WAIT_FOREVER);
        bios.prompt_open = 0;
        /* The screen reuses the row; the serial line moves on. */
        if (bios.fb_ok) {
            console_puts("\r                                \r");
        }
        uart_putc('\n');
        if (ev.type == INPUT_EV_CHAR && (ev.code == 'q' || ev.code == 'Q')) {
            stopped = 1;
            bios_say("Stopped.");
            return -1;
        }
    }
    bios_say(line);
    return 0;
}

static void erase(unsigned n)
{
    while (n-- > 0) {
        echo('\b');
        echo(' ');
        echo('\b');
    }
}

/*
 * Reads one line into buf. Backspace, and UP and DOWN through the last
 * HISTORY lines. A character past SH_LINE_MAX is refused with a bell on
 * the serial line: the line stays in one row and stays editable.
 */
static void read_line(char *buf)
{
    input_event_t ev;
    unsigned len = 0;
    unsigned h = history_len;

    bios_put(SH_PROMPT);
    bios.prompt_open = 1;
    if (bios.fb_ok) {
        console_cursor_show(1);
    }
    for (;;) {
        if (!shell_key(&ev, INPUT_WAIT_FOREVER)) {
            continue;
        }
        if (ev.type == INPUT_EV_CHAR) {
            if (ev.code >= 0x20 && ev.code < 0x7f) {
                if (len < SH_LINE_MAX) {
                    buf[len++] = (char)ev.code;
                    echo((char)ev.code);
                } else {
                    uart_putc('\a');
                }
            }
            continue;
        }
        switch (ev.code) {
        case INPUT_KEY_SELECT:
            buf[len] = '\0';
            if (bios.fb_ok) {
                console_cursor_show(0);
            }
            echo('\n');
            bios.prompt_open = 0;
            return;
        case INPUT_KEY_BACK:
            if (len > 0) {
                len--;
                erase(1);
            }
            break;
        case INPUT_KEY_UP:
        case INPUT_KEY_DOWN:
            if (ev.code == INPUT_KEY_UP && h > 0) {
                h--;
            } else if (ev.code == INPUT_KEY_DOWN && h < history_len) {
                h++;
            } else {
                break;
            }
            erase(len);
            len = 0;
            if (h < history_len) {
                for (; history[h][len] != '\0'; len++) {
                    buf[len] = history[h][len];
                    echo(buf[len]);
                }
            }
            break;
        default:
            break;
        }
    }
}

static void remember(const char *line)
{
    unsigned i;

    if (line[0] == '\0') {
        return;
    }
    if (history_len == HISTORY) {
        for (i = 1; i < HISTORY; i++) {
            k_memcpy(history[i - 1], history[i], sizeof(history[i]));
        }
        history_len--;
    }
    k_snprintf(history[history_len++], sizeof(history[0]), "%s", line);
}

/* "/x" for "x": paths are on the boot volume, from its root. Returns -1 when too long. */
static int full_path(char *out, size_t size, const char *path)
{
    int n = k_snprintf(out, size, "%s%s", path[0] == '/' ? "" : "/", path);

    return n < 0 || (size_t)n >= size ? -1 : 0;
}

/* ---- Commands. Each returns the rc of its shell_cmd marker: "0" or a short name. ---- */

static const char *cmd_help(const sh_cmd_t *c)
{
    const char *line;
    unsigned i;

    for (i = 0; (line = c->topic >= 0 ? sh_usage_line(c->topic, i) : sh_help_line(i)) != NULL;
         i++) {
        if (shell_say(line) != 0) {
            break;
        }
    }
    return "0";
}

static const char *cmd_info(void)
{
    const robco_board_t *b = board_get();
    char line[BIOS_LINE_MAX];
    char err[BIOS_LINE_MAX];
    uint32_t up = (uint32_t)(timer_now_us() / 1000000u);

    bootlines_title(line, sizeof(line), RETROS_VERSION_STR);
    shell_say(line);
    bootlines_board(line, sizeof(line), b->board_rev, b->soc_name);
    shell_say(line);
    k_snprintf(line, sizeof(line), "Memory: %u KB ARM RAM at 0x%08x, peripherals at 0x%08x",
               (unsigned)(b->mem_size / 1024u), (unsigned)b->mem_base, (unsigned)b->periph_base);
    shell_say(line);
    if (!bios.mounted) {
        shell_say("Boot volume: none");
    } else {
        if (bios.vol.slot == FS_BOOT_WHOLE) {
            k_snprintf(line, sizeof(line), "Boot volume: whole card, %u blocks",
                       (unsigned)bios.vol.block_count);
        } else {
            k_snprintf(line, sizeof(line), "Boot volume: part %u, LBA %u, %u blocks",
                       (unsigned)bios.vol.slot, (unsigned)bios.vol.start_lba,
                       (unsigned)bios.vol.block_count);
        }
        shell_say(line);
        /* Still mounted, but the last POST could not use it: the card stopped answering. */
        if (bios.post.item[POST_FAT].result != POST_OK) {
            shell_say("Boot volume card not answering. Put a card in and run POST.");
        }
    }
    bootlines_settings(line, sizeof(line), bios.settings.delay_ms, bios.settings.beep,
                       bios_scheme_name(bios.settings.scheme),
                       bios.cfg_source != NULL ? bios.cfg_source : "defaults");
    shell_say(line);
    if (bios.error != 0) {
        bios_error_line(err, sizeof(err), bios.error, NULL);
        k_snprintf(line, sizeof(line), "Last error: %s", err + 6);
    } else {
        k_snprintf(line, sizeof(line), "Last error: none");
    }
    shell_say(line);
    k_snprintf(line, sizeof(line), "Uptime: %u h %02u min %02u s", (unsigned)(up / 3600u),
               (unsigned)(up / 60u % 60u), (unsigned)(up % 60u));
    shell_say(line);
    if (bios.selfcheck == RIMG_OK) {
        k_snprintf(line, sizeof(line), "Self check: passed");
    } else {
        k_snprintf(line, sizeof(line), "Self check: FAILED, %s", rimg_strerror(bios.selfcheck));
    }
    shell_say(line);
    return "0";
}

static const char *cmd_post(void)
{
    bios_post_table();
    return bios.post_fatal != 0 ? bios_err_name(bios.post_fatal) : "0";
}

static const char *cmd_vol(void)
{
    part_info_t parts[PART_MAX];
    char line[BIOS_LINE_MAX];
    int n, i, marked = 0;

    if (bios.post.item[POST_SD].result != POST_OK) {
        shell_say("No SD card. Run POST to try again.");
        return "io";
    }
    n = part_scan(sd_blkdev(), parts, PART_MAX);
    if (n < 0) {
        k_snprintf(line, sizeof(line), "Block 0 does not read: error %d.", n);
        shell_say(line);
        return "io";
    }
    if (n == 0) {
        shell_say("No partition table and no FAT boot sector.");
        return "0";
    }
    shell_say("SLOT  TYPE  START LBA     BLOCKS  FAT  BOOT");
    for (i = 0; i < n; i++) {
        const part_info_t *p = &parts[i];
        int chosen = bios.mounted && bootvol_is(&bios.vol, p);

        marked |= chosen;

        if (p->superfloppy) {
            k_snprintf(line, sizeof(line), "whole  --  %9u %10u  %-3s  %s", 0u,
                       (unsigned)p->block_count, "yes", chosen ? "*" : "");
        } else {
            k_snprintf(line, sizeof(line), "%4u  0x%02x  %9u %10u  %-3s  %s", (unsigned)p->index,
                       (unsigned)p->type, (unsigned)p->start_lba, (unsigned)p->block_count,
                       part_boot_class(p) == PART_BOOT_FAT ? "yes" : "no", chosen ? "*" : "");
        }
        shell_say(line);
    }
    if (!bios.mounted) {
        shell_say("No boot volume is mounted.");
    } else if (marked) {
        shell_say("* is the boot volume.");
    } else {
        shell_say("The boot volume is not on this card. Run POST.");
    }
    return "0";
}

static fs_stat_t entries[DIR_MAX];

static const char *fs_rc(int rc)
{
    switch (rc) {
    case FS_ERR_NOENT: return "nofile";
    case FS_ERR_ISDIR: return "isdir";
    default:           return "io";
    }
}

static int readdir_next(void *ctx, fs_stat_t *out)
{
    return fs_readdir((fs_dir_t *)ctx, out);
}

static const char *cmd_dir(const sh_cmd_t *c)
{
    static fs_stat_t tmp;
    char path[FS_PATH_MAX + 1];
    char line[BIOS_LINE_MAX];
    fs_dir_t d;
    sh_dir_totals_t tot;
    unsigned n = 0, more = 0, j;
    int rc;

    if (!bios.mounted) {
        shell_say("No boot volume.");
        return "novol";
    }
    if (full_path(path, sizeof(path), c->path != NULL ? c->path : SH_DIR_DEFAULT) != 0) {
        shell_say("Path is too long.");
        return "usage";
    }
    /* The root has no entry of its own to stat. */
    rc = path[1] != '\0' ? fs_stat(path, &tmp) : 0;
    if (rc == 0 && path[1] != '\0' && !tmp.is_dir) {
        k_snprintf(line, sizeof(line), "Not a folder: %.60s", path);
        shell_say(line);
        return "notdir";
    }
    if (rc == 0) {
        rc = fs_opendir(&d, path);
    }
    if (rc != 0) {
        if (rc == FS_ERR_NOENT) {
            k_snprintf(line, sizeof(line), "Not found: %.60s", path);
        } else {
            k_snprintf(line, sizeof(line), "Cannot list %.50s: error %d", path, rc);
        }
        shell_say(line);
        return fs_rc(rc);
    }
    rc = sh_dir_read(readdir_next, &d, entries, DIR_MAX, &tmp, &n, &more);
    fs_closedir(&d);
    for (j = 0; j < n; j++) {
        const fs_stat_t *e = &entries[j];

        if (e->is_dir) {
            k_snprintf(line, sizeof(line), "%-60.60s      <DIR>", e->name);
        } else {
            k_snprintf(line, sizeof(line), "%-60.60s %10u", e->name, (unsigned)e->size);
        }
        if (shell_say(line) != 0) {
            return rc < 0 ? fs_rc(rc) : "0";
        }
    }
    if (rc < 0) {
        /* A partial listing: no totals, which would look like a whole folder. */
        k_snprintf(line, sizeof(line), "Read error after %u entries: error %d", n + more, rc);
        shell_say(line);
        return fs_rc(rc);
    }
    if (more != 0) {
        k_snprintf(line, sizeof(line), "%u more entries not shown: the list holds %u.", more,
                   DIR_MAX);
        shell_say(line);
    }
    sh_dir_count(entries, n, &tot);
    sh_dir_count_line(line, sizeof(line), &tot);
    shell_say(line);
    return "0";
}

static uint8_t type_buf[TYPE_MAX];

static const char *cmd_type(const sh_cmd_t *c)
{
    char path[FS_PATH_MAX + 1];
    char line[BIOS_LINE_MAX];
    fs_stat_t *st = &entries[0];
    fs_file_t f;
    unsigned col = 0;
    uint32_t i, got = 0;
    int rc;

    if (!bios.mounted) {
        shell_say("No boot volume.");
        return "novol";
    }
    if (full_path(path, sizeof(path), c->path) != 0) {
        shell_say("Path is too long.");
        return "usage";
    }
    rc = fs_stat(path, st);
    if (rc == 0 && st->is_dir) {
        rc = FS_ERR_ISDIR;
    }
    if (rc == 0) {
        rc = fs_open(&f, path, FS_READ);
    }
    if (rc == 0) {
        uint32_t want = st->size < TYPE_MAX ? st->size : TYPE_MAX;

        while (got < want && (rc = fs_read(&f, type_buf + got, want - got)) > 0) {
            got += (uint32_t)rc;
        }
        fs_close(&f);
        if (rc == 0 && got < want) {
            /* The read ended before the size the folder entry gives. */
            k_snprintf(line, sizeof(line), "Read error: %.28s ends at %u of %u bytes", path,
                       (unsigned)got, (unsigned)st->size);
            shell_say(line);
            return "io";
        }
    }
    if (rc < 0) {
        if (rc == FS_ERR_NOENT || rc == FS_ERR_ISDIR) {
            k_snprintf(line, sizeof(line), "%s: %.60s",
                       rc == FS_ERR_NOENT ? "Not found" : "Is a folder", path);
        } else {
            k_snprintf(line, sizeof(line), "Read error: %.48s: error %d", path, rc);
        }
        shell_say(line);
        return fs_rc(rc);
    }
    for (i = 0; i < got; i++) {
        char ch = sh_type_char(type_buf[i]);

        if (ch == 0) {
            continue;
        }
        if (ch == '\t') {
            ch = ' ';
        }
        if (ch != '\n') {
            line[col++] = ch;
        }
        if (ch == '\n' || col == BIOS_LINE_MAX - 1) {
            line[col] = '\0';
            col = 0;
            if (shell_say(line) != 0) {
                return "0";
            }
        }
    }
    if (col > 0) {
        line[col] = '\0';
        shell_say(line);
    }
    if (st->size > TYPE_MAX) {
        k_snprintf(line, sizeof(line), "-- cut: the file has %u bytes, %u shown --",
                   (unsigned)st->size, TYPE_MAX);
        shell_say(line);
    }
    return "0";
}

static const char *cmd_mem(const sh_cmd_t *c)
{
    const robco_board_t *b = board_get();
    char line[SH_HEX_LINE];
    uint8_t bytes[16];
    uint32_t done, bad = 0;
    int r = sh_mem_check(c->addr, c->len, b->mem_base, b->mem_size, b->periph_base);

    if (r == SH_MEM_DEVICE) {
        shell_say("Device address refused: a read can change the device state.");
        return "device";
    }
    if (r != SH_MEM_OK) {
        k_snprintf(line, sizeof(line), "Outside RAM: RAM is 0x%08x to 0x%08x.",
                   (unsigned)b->mem_base, (unsigned)(b->mem_base + b->mem_size - 1u));
        shell_say(line);
        return "outside";
    }
    for (done = 0; done < c->len; done += 16u) {
        uint32_t addr = c->addr + done;
        unsigned n = c->len - done < 16u ? (unsigned)(c->len - done) : 16u;
        uint32_t valid = 0;
        unsigned i;

        for (i = 0; i < n; i++) {
            uint32_t a = addr + i;
            uint32_t w;

            /* An abort-safe read of the word that holds the byte. */
            if (mmio_probe(a & ~3u, &w) == 0) {
                bytes[i] = (uint8_t)(w >> (8u * (a & 3u)));
                valid |= 1u << i;
            } else {
                bad++;
            }
        }
        sh_hex_line(line, sizeof(line), addr, bytes, n, valid);
        if (shell_say(line) != 0) {
            return "0";
        }
    }
    if (bad != 0) {
        k_snprintf(line, sizeof(line), "%u bytes did not read: shown as --.", (unsigned)bad);
        shell_say(line);
        return "abort";
    }
    return "0";
}

static const char *cmd_boot(const sh_cmd_t *c)
{
    char path[LOADER_PATH_MAX];
    int rc;

    if (c->path != NULL && full_path(path, sizeof(path), c->path) != 0) {
        shell_say("Path is too long: at most 63 characters.");
        return "usage";
    }
    /* The operator's choice: BOOT still loads after a failed memory test, with the warning. */
    if (bios.post_fatal == BIOS_ERR_MEMTEST) {
        bios_say(bootlines_memtest_warning());
    }
    rc = bios_boot(c->path != NULL ? path : NULL);
    return bios_err_name(rc);
}

static void mark_cmd(const char *name, const char *rc)
{
    BIOS_MARK("shell_cmd name=%s rc=%s", name, rc);
}

void shell_run(const char *reason, int keys)
{
    static char buf[SH_LINE_MAX + 1];
    char err[SH_ERR_MAX];
    sh_cmd_t c;
    int r;

    from_keys = keys;
    bios.phase = BIOS_PHASE_SHELL;
    bios_say("");
    bios_say("RETROS emergency shell. Type HELP for the commands.");
    BIOS_MARK("shell reason=%s available=1", reason);
    for (;;) {
        const char *rc = "0";

        read_line(buf);
        remember(buf);
        pager_start();
        r = sh_parse(buf, &c, err, sizeof(err));
        if (r == SH_EMPTY) {
            continue;
        }
        if (r == SH_BAD) {
            shell_say(err);
            mark_cmd(c.id >= 0 ? sh_marker_name(c.id) : "unknown",
                     c.id >= 0 ? "usage" : "unknown");
            continue;
        }
        switch (c.id) {
        case SH_HELP:
            rc = cmd_help(&c);
            break;
        case SH_INFO:
            rc = cmd_info();
            break;
        case SH_POST:
            rc = cmd_post();
            break;
        case SH_VOL:
            rc = cmd_vol();
            break;
        case SH_DIR:
            rc = cmd_dir(&c);
            break;
        case SH_TYPE:
            rc = cmd_type(&c);
            break;
        case SH_MEM:
            rc = cmd_mem(&c);
            break;
        case SH_BOOT:
            rc = cmd_boot(&c);
            break;
        case SH_CONTINUE:
            /* The rule of the power-on boot: it goes on only after a POST with no fatal result. */
            if (from_keys && bios.post_fatal != 0) {
                char line[BIOS_LINE_MAX];

                post_continue_refused(line, sizeof(line), &bios.post);
                shell_say(line);
                rc = bios_err_name(bios.post_fatal);
                break;
            }
            if (from_keys) {
                bios_say("Going on with the boot.");
                mark_cmd("continue", "0");
                bios.phase = BIOS_PHASE_LOAD;
                return;
            }
            shell_say("Nothing to continue: the boot stopped. Use BOOT or REBOOT.");
            rc = "refused";
            break;
        case SH_DIAG:
            rc = diag_command(&c) == 0 ? "0" : "fail";
            break;
        case SH_REBOOT:
            bios_say("Restarting.");
            mark_cmd("reboot", "0");
            BIOS_MARK("reboot");
            uart_flush();
            power_reboot();
        case SH_CLS:
            if (bios.fb_ok) {
                console_clear();
            }
            break;
        default:
            break;
        }
        mark_cmd(sh_marker_name(c.id), rc);
    }
}
