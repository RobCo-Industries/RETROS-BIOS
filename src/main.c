/*
 * RETROS-BIOS: the first stage of the terminal chain. Runs the power-on
 * self test, shows the boot screen, loads MFBootAgent from the SD card
 * and enters it as docs/spec/BOOT_PROTOCOL.md in RobCo-Core says.
 */

#include <stdarg.h>
#include <stdint.h>

#include <robco/audio.h>
#include <robco/board.h>
#include <robco/boot.h>
#include <robco/bootinfo.h>
#include <robco/console.h>
#include <robco/fb.h>
#include <robco/fs.h>
#include <robco/input.h>
#include <robco/irq.h>
#include <robco/klibc.h>
#include <robco/loader.h>
#include <robco/rimg.h>
#include <robco/tick.h>
#include <robco/timer.h>
#include <robco/uart.h>

#include "bios.h"
#include "boot.h"
#include "bootlines.h"
#include "post.h"
#include "settings.h"

#define TICK_HZ 100

bios_state_t bios;

static bios_settings_t settings;
static loader_result_t res;
static char cfg_text[BIOS_CFG_MAX];

/* The power-on beep: high, low, middle. */
static const audio_note_t beep_notes[] = {
    { 800, 150 }, { 0, 50 }, { 400, 150 }, { 0, 50 }, { 600, 200 },
};

void bios_mark(const char *fmt, ...)
{
    char buf[192];
    va_list ap;

    va_start(ap, fmt);
    k_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    k_printf("@RC stage=" ROBCO_STAGE_NAME " ev=%s\n", buf);
}

void bios_say(const char *line)
{
    if (bios.fb_ok) {
        console_puts(line);
        console_putc('\n');
    }
    uart_puts(line);
    uart_putc('\n');
}

static const char *mode_name(uint32_t mode)
{
    switch (mode) {
    case 0x13: return "svc";
    case 0x1a: return "hyp";
    default:   return "other";
    }
}

/* Stops the boot: the error is on screen and in its markers already. */
static void halt_boot(int code)
{
    bios.error = code;
    bios_say(bootlines_halted());
    BIOS_MARK("shell reason=%s available=0", bios_err_name(code));
    maint_idle();
}

static void show_error(int code, const char *path)
{
    char line[BIOS_LINE_MAX];

    bios_error_line(line, sizeof(line), code, path);
    bios_say(line);
}

/* Step 1: console, maintenance port, screen, tick and IRQs. */
static void init_hw(void)
{
    const robco_board_t *b = board_get();
    int fb;
    int con = -1;
    int tick;

    uart_init(115200);
    k_printf("\n");
    BIOS_MARK("boot midr=0x%08x soc=%s base=0x%08x board_rev=0x%08x mem=0x%08x entry_mode=%s "
              "version=" RETROS_VERSION_STR,
              (unsigned)b->midr, b->soc_name, (unsigned)b->periph_base, (unsigned)b->board_rev,
              (unsigned)b->mem_size, mode_name(robco_boot_args()->entry_mode));

    fb = fb_init(640, 480, 32);
    if (fb == 0) {
        con = console_init(CONSOLE_SCHEME_GREEN);
    }
    bios.fb_ok = fb == 0 && con == 0;

    irq_init();
    tick = tick_init(TICK_HZ);
    irq_cpu_enable();
    BIOS_MARK("init fb=%d console=%d tick=%d hz=%u", fb, con, tick, (unsigned)tick_hz());
    maint_start();
}

/* Step 2: the boot text. */
static void banner(void)
{
    const robco_board_t *b = board_get();
    char line[BIOS_LINE_MAX];
    const char *lore;
    unsigned i;

    bootlines_title(line, sizeof(line), RETROS_VERSION_STR);
    bios_say(line);
    for (i = 0; (lore = bootlines_lore(i)) != NULL; i++) {
        bios_say(lore);
    }
    bootlines_memory(line, sizeof(line), b->mem_size);
    bios_say(line);
    bootlines_board(line, sizeof(line), b->board_rev, b->soc_name);
    bios_say(line);
    bios_say("");
}

/* Step 3: the self test and its table. */
static void post(void)
{
    char line[BIOS_LINE_MAX];
    post_counts_t c;
    uint64_t t0 = timer_now_us();
    unsigned i;
    int fatal;

    bios.phase = BIOS_PHASE_POST;
    bios_say(bootlines_post_title());
    post_run();
    for (i = 0; i < POST_COUNT; i++) {
        bootlines_post_row(line, sizeof(line), i, &bios.post.item[i]);
        bios_say(line);
    }
    post_count(&bios.post, &c);
    bootlines_post_summary(line, sizeof(line), &c);
    bios_say(line);
    fatal = post_fatal(&bios.post);
    bios.post_fatal = fatal;
    BIOS_MARK("post_done result=%s ok=%u fail=%u absent=%u us=%u", fatal != 0 ? "fail" : "ok",
              c.ok, c.fail, c.absent, (unsigned)(timer_now_us() - t0));
    if (fatal == BIOS_ERR_MEMTEST) {
        BIOS_MARK("error code=memtest");
        show_error(fatal, NULL);
        halt_boot(fatal);
    }
    if (fatal == BIOS_ERR_IO) {
        const char *what = bios.post.item[POST_SD].result != POST_OK ? "sd" : "fat";

        BIOS_MARK("error code=io path=" BIOS_PRIMARY " what=%s", what);
        show_error(fatal, BIOS_PRIMARY);
        halt_boot(fatal);
    }
}

/* Settings from BOOT.CFG. Runs after the POST mounted the card, before the beep. */
static void read_settings(void)
{
    char line[BIOS_LINE_MAX];
    bios_cfg_report_t r;
    const char *source = "defaults";
    size_t len = 0;
    unsigned err_line = 0;
    int rc;

    bios_settings_default(&settings);
    rc = fs_read_file(BIOS_CFG_PATH, cfg_text, sizeof(cfg_text), &len);
    if (rc == FS_ERR_NOENT) {
        BIOS_MARK("config result=missing path=" BIOS_CFG_PATH);
    } else if (rc != 0) {
        BIOS_MARK("config result=bad path=" BIOS_CFG_PATH " why=read rc=%d", rc);
    } else if ((rc = bios_settings_parse(cfg_text, len, &settings, &r, &err_line)) != 0) {
        BIOS_MARK("config result=bad path=" BIOS_CFG_PATH " why=syntax line=%u", err_line);
    } else {
        source = BIOS_CFG_PATH;
        BIOS_MARK("config result=ok path=" BIOS_CFG_PATH " applied=%u clamped=%u bad=%u unknown=%u",
                  r.applied, r.clamped, r.bad, r.unknown);
    }
    BIOS_MARK("settings delay_ms=%u beep=%d scheme=%s", (unsigned)settings.delay_ms,
              settings.beep, bios_scheme_name(settings.scheme));
    if (bios.fb_ok && settings.scheme != console_scheme()) {
        console_set_scheme(settings.scheme);
    }
    bootlines_settings(line, sizeof(line), settings.delay_ms, settings.beep,
                       bios_scheme_name(settings.scheme), source);
    bios_say(line);
}

/* Step 4: the power-on beep. It plays on while the key window runs. */
static void beep(void)
{
    int rc;

    if (!settings.beep) {
        BIOS_MARK("beep state=off");
        return;
    }
    if (bios.post.item[POST_AUDIO].result != POST_OK) {
        BIOS_MARK("beep state=skipped reason=%s",
                  post_result_name(bios.post.item[POST_AUDIO].result));
        return;
    }
    rc = audio_play(beep_notes, sizeof(beep_notes) / sizeof(beep_notes[0]), 0);
    BIOS_MARK("beep state=%s rc=%d", rc == 0 ? "played" : "failed", rc);
}

/* Step 6: the key window. Any key on the console or the maintenance port. */
static void key_window(void)
{
    input_event_t ev;
    uint64_t end;
    uint64_t now;
    const char *source = NULL;

    bios.phase = BIOS_PHASE_KEYS;
    BIOS_MARK("key_window ms=%u", (unsigned)settings.delay_ms);
    if (settings.delay_ms == 0) {
        /* No prompt on the screen: no key can come. The end marker closes the window. */
        BIOS_MARK("key_window_end key=0");
        return;
    }
    bios_say(bootlines_key_prompt());
    end = timer_now_us() + (uint64_t)settings.delay_ms * 1000u;
    while ((now = timer_now_us()) < end) {
        uint32_t left = (uint32_t)((end - now + 999u) / 1000u);

        audio_poll();
        if (input_wait(&ev, left) == 0) {
            continue;
        }
        if (ev.type == INPUT_EV_CHAR || (ev.type == INPUT_EV_KEY && ev.value == INPUT_PRESS)) {
            source = ev.source == INPUT_SRC_SCRIPT ? "maint" : "console";
            break;
        }
    }
    audio_poll();
    if (source != NULL) {
        bios.shell_requested = 1;
        BIOS_MARK("shell_request source=%s", source);
        bios_say(bootlines_shell_not_built());
    }
    BIOS_MARK("key_window_end key=%d", bios.shell_requested);
}

/* Step 7: loads MFBootAgent, or its backup, into the slot. */
static void load(void)
{
    const loader_slot_t slot = { BIOS_SLOT_BASE, BIOS_SLOT_SIZE, NULL };
    const rimg_expect_t expect = { .magic = RIMG_MAGIC_BOOT, .abi_major = BIOS_NEXT_ABI };
    char line[BIOS_LINE_MAX];
    char name[sizeof(res.hdr->name) + 1];
    int rc;

    bios.phase = BIOS_PHASE_LOAD;
    bootlines_loading(line, sizeof(line), BIOS_PRIMARY);
    bios_say(line);
    rc = loader_load_fallback(BIOS_PRIMARY, BIOS_BACKUP, &slot, &expect, &res);
    if (res.fallback) {
        BIOS_MARK("error code=%s path=" BIOS_PRIMARY, bios_err_name(res.primary_err));
        show_error(res.primary_err, BIOS_PRIMARY);
        BIOS_MARK("fallback path=" BIOS_BACKUP " from=" BIOS_PRIMARY " to=" BIOS_BACKUP
                  " reason=%s", bios_err_name(res.primary_err));
        bootlines_loading(line, sizeof(line), BIOS_BACKUP);
        bios_say(line);
    }
    if (rc != RIMG_OK) {
        BIOS_MARK("error code=%s path=%s", bios_err_name(rc), res.path);
        show_error(rc, res.path);
        audio_wait();
        halt_boot(rc);
    }
    memcpy(name, res.hdr->name, sizeof(res.hdr->name));
    name[sizeof(res.hdr->name)] = '\0';
    BIOS_MARK("load path=%s size=%u crc=0x%08x addr=0x%08x name=%s", res.path,
              (unsigned)res.file_size, (unsigned)res.crc32, (unsigned)res.entry, name);
    bootlines_loaded(line, sizeof(line), name, res.file_size, res.crc32);
    bios_say(line);
    bootlines_start(line, sizeof(line), name, res.entry);
    bios_say(line);
}

/* Step 8: the boot info and the jump. */
static void handoff(void)
{
    robco_bootinfo_t *bi = (robco_bootinfo_t *)ROBCO_BOOTINFO_ADDR;
    uint32_t fdt_size;

    fs_unmount("sd");
    bios.mounted = 0;
    audio_wait();
    audio_stop();
    fdt_size = bootinfo_copy_fw_blob(ROBCO_HANDOFF_FDT_ADDR, ROBCO_HANDOFF_FDT_MAX);
    bootinfo_build(bi, ROBCO_STAGE_BIOS);
    bios_bootinfo_set_boot(bi, ROBCO_BOOT_DEV_EMMC, (uint32_t)bios.part.start_lba, res.fallback,
                           res.path);
    BIOS_MARK("bootinfo flags=0x%08x caps=0x%08x dev=%u part_lba=%u fdt_size=%u",
              (unsigned)bi->boot_flags, (unsigned)bi->caps, (unsigned)bi->boot_dev,
              (unsigned)bi->boot_part_lba, (unsigned)fdt_size);
    BIOS_MARK("jump entry=0x%08x", (unsigned)res.entry);
    uart_flush();
    robco_handoff(res.entry, bi, fdt_size != 0 ? ROBCO_HANDOFF_FDT_ADDR : 0);
}

void robco_main(void)
{
    bios.phase = BIOS_PHASE_INIT;
    init_hw();
    banner();
    post();
    read_settings();
    beep();
    key_window();
    load();
    handoff();
}
