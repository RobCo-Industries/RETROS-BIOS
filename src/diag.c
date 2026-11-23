/*
 * DIAG: the diagnostics of the emergency shell. Each test fills a result
 * and a detail, prints one `diag` marker with its numbers, and the shell
 * shows one row per test, like the POST table.
 */

#include <stdarg.h>
#include <stdint.h>

#include <robco/audio.h>
#include <robco/blk.h>
#include <robco/board.h>
#include <robco/console.h>
#include <robco/datetime.h>
#include <robco/fb.h>
#include <robco/input.h>
#include <robco/irq.h>
#include <robco/klibc.h>
#include <robco/rng.h>
#include <robco/rtc.h>
#include <robco/sd.h>
#include <robco/tick.h>
#include <robco/timer.h>

#include "bios.h"
#include "boot.h"
#include "diag.h"
#include "diagcalc.h"
#include "post.h"

#define DETAIL_MAX  64
#define VIDEO_MS    3000u
#define INPUT_MS    10000u
#define SD_CHUNK    8u
#define RNG_WORDS   1024u
#define PCM_RATE    8000u
#define PCM_LEN     2000u

typedef struct {
    int result;                 /* POST_OK, POST_FAIL, POST_ABSENT or POST_SKIPPED */
    char detail[DETAIL_MAX];
} diag_out_t;

static void set(diag_out_t *o, int result, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void set(diag_out_t *o, int result, const char *fmt, ...)
{
    va_list ap;

    o->result = result;
    va_start(ap, fmt);
    k_vsnprintf(o->detail, sizeof(o->detail), fmt, ap);
    va_end(ap);
}

/* Shows progress on the screen only, on one line that each call rewrites. */
static void progress(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void progress(const char *fmt, ...)
{
    char line[BIOS_LINE_MAX];
    va_list ap;

    if (!bios.fb_ok) {
        return;
    }
    va_start(ap, fmt);
    k_vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    console_putc('\r');
    console_puts(line);
}

static void progress_end(void)
{
    if (bios.fb_ok) {
        console_puts("\r                                                  \r");
    }
}

/* ---- MEM ---- */

static int mem_pass(volatile uint32_t *base, uint32_t size, int pat, int fill,
                    diag_mem_fault_t *f, int *stop)
{
    uint32_t mib = size / DIAG_MIB + (size % DIAG_MIB != 0);
    uint32_t off;

    for (off = 0; off < size; off += DIAG_MIB) {
        uint32_t n = size - off < DIAG_MIB ? size - off : DIAG_MIB;
        volatile uint32_t *p = base + off / 4u;
        int rc = 0;

        if (pat == DIAG_PAT_WALK) {
            rc = diag_mem_walk(p, n / 4u, f);
        } else if (fill) {
            diag_mem_fill(p, n / 4u, pat);
        } else {
            rc = diag_mem_check(p, n / 4u, pat, f);
        }
        if (rc != 0) {
            return -1;
        }
        progress("DIAG MEM %s %s: %u of %u MiB", diag_pat_name(pat),
                 pat == DIAG_PAT_WALK ? "test" : fill ? "write" : "read",
                 (unsigned)(off / DIAG_MIB + 1u), (unsigned)mib);
        if (shell_key_pending()) {
            *stop = 1;
            return 0;
        }
    }
    return 0;
}

static void diag_mem(const sh_cmd_t *c, int all, diag_out_t *o)
{
    const robco_board_t *b = board_get();
    const fb_info_t *fb = fb_get();
    uint32_t fb_addr = fb != NULL ? (uint32_t)(uintptr_t)fb->addr : 0;
    uint32_t fb_size = fb != NULL ? fb->size : 0;
    uint32_t base, size;
    diag_mem_fault_t f;
    uint64_t t0;
    int stop = 0;
    int pat;

    if (c != NULL && c->has_range) {
        if (!diag_mem_allowed(c->addr, c->len, b->mem_base, b->mem_size, fb_addr, fb_size)) {
            set(o, POST_SKIPPED, "window not allowed, see HELP DIAG");
            BIOS_MARK("diag name=mem result=skipped reason=window addr=0x%08x len=0x%08x",
                      (unsigned)c->addr, (unsigned)c->len);
            return;
        }
        base = c->addr;
        size = c->len;
    } else if (diag_mem_window(b->mem_base, b->mem_size, fb_addr, fb_size, &base, &size) != 0) {
        set(o, POST_SKIPPED, "no RAM above 0x%08x", (unsigned)DIAG_MEM_LOW);
        BIOS_MARK("diag name=mem result=skipped reason=window");
        return;
    }
    if (all && size > DIAG_ALL_MEM_SIZE) {
        size = DIAG_ALL_MEM_SIZE;
    }
    t0 = timer_now_us();
    for (pat = 0; pat < DIAG_PAT_COUNT && !stop; pat++) {
        int bad = mem_pass((volatile uint32_t *)(uintptr_t)base, size, pat, 1, &f, &stop);

        if (bad == 0 && !stop && pat != DIAG_PAT_WALK) {
            bad = mem_pass((volatile uint32_t *)(uintptr_t)base, size, pat, 0, &f, &stop);
        }
        if (bad != 0) {
            progress_end();
            set(o, POST_FAIL, "%s at 0x%08x: 0x%08x, not 0x%08x", diag_pat_name(f.pat),
                (unsigned)f.addr, (unsigned)f.actual, (unsigned)f.expected);
            BIOS_MARK("diag name=mem result=fail base=0x%08x size=0x%08x pattern=%s addr=0x%08x "
                      "expected=0x%08x actual=0x%08x", (unsigned)base, (unsigned)size,
                      diag_pat_name(f.pat), (unsigned)f.addr, (unsigned)f.expected,
                      (unsigned)f.actual);
            return;
        }
    }
    progress_end();
    if (stop) {
        set(o, POST_SKIPPED, "stopped by a key in %s", diag_pat_name(pat - 1));
        BIOS_MARK("diag name=mem result=skipped reason=key base=0x%08x size=0x%08x",
                  (unsigned)base, (unsigned)size);
        return;
    }
    if (size >= DIAG_MIB) {
        set(o, POST_OK, "%u MiB at 0x%08x, 4 patterns", (unsigned)(size / DIAG_MIB),
            (unsigned)base);
    } else {
        set(o, POST_OK, "%u bytes at 0x%08x, 4 patterns", (unsigned)size, (unsigned)base);
    }
    BIOS_MARK("diag name=mem result=ok base=0x%08x size=0x%08x us=%u", (unsigned)base,
              (unsigned)size, (unsigned)(timer_now_us() - t0));
}

/* ---- SD ---- */

static uint8_t sd_buf[SD_CHUNK * 512u] __attribute__((aligned(64)));

static void diag_sd(const sh_cmd_t *c, diag_out_t *o)
{
    uint32_t count = c != NULL ? c->count : SH_SD_DEFAULT;
    uint32_t lba = bios.mounted ? (uint32_t)bios.part.start_lba : 0;
    uint32_t done, errors = 0, first_lba = 0;
    int first_rc = 0;
    uint64_t t0, us;

    if (bios.post.item[POST_SD].result != POST_OK) {
        set(o, POST_ABSENT, "no card");
        BIOS_MARK("diag name=sd result=absent");
        return;
    }
    if (bios.mounted && count > bios.part.block_count) {
        count = (uint32_t)bios.part.block_count;
    }
    t0 = timer_now_us();
    for (done = 0; done < count; done += SD_CHUNK) {
        uint32_t n = count - done < SD_CHUNK ? count - done : SD_CHUNK;
        int rc = blk_read(sd_blkdev(), lba + done, n, sd_buf);

        if (rc != 0) {
            if (errors == 0) {
                first_rc = rc;
                first_lba = lba + done;
            }
            errors++;
        }
    }
    us = timer_now_us() - t0;
    if (errors != 0) {
        set(o, POST_FAIL, "%u read errors, first %d at LBA %u", (unsigned)errors, first_rc,
            (unsigned)first_lba);
    } else {
        set(o, POST_OK, "%u blocks at LBA %u, %u KiB/s", (unsigned)count, (unsigned)lba,
            (unsigned)diag_kib_per_s((uint64_t)count * 512u, us));
    }
    BIOS_MARK("diag name=sd result=%s lba=%u blocks=%u errors=%u us=%u kib_s=%u",
              errors != 0 ? "fail" : "ok", (unsigned)lba, (unsigned)count, (unsigned)errors,
              (unsigned)us, (unsigned)diag_kib_per_s((uint64_t)count * 512u, us));
}

/* ---- VIDEO ---- */

static const uint8_t bars[8][3] = {
    { 255, 255, 255 }, { 255, 255, 0 }, { 0, 255, 255 }, { 0, 255, 0 },
    { 255, 0, 255 }, { 255, 0, 0 }, { 0, 0, 255 }, { 0, 0, 0 },
};

static void diag_video(diag_out_t *o)
{
    const fb_info_t *fb = fb_get();
    input_event_t ev;
    uint32_t w, h, x, y, grid, white;
    unsigned i;
    int key;

    if (!bios.fb_ok || fb == NULL) {
        set(o, POST_ABSENT, "no screen");
        BIOS_MARK("diag name=video result=absent");
        return;
    }
    w = fb->width;
    h = fb->height;
    grid = fb_rgb(96, 96, 96);
    white = fb_rgb(255, 255, 255);
    for (i = 0; i < 8; i++) {
        fb_fill_rect(i * w / 8u, 0, (i + 1u) * w / 8u - i * w / 8u, h,
                     fb_rgb(bars[i][0], bars[i][1], bars[i][2]));
    }
    for (x = 40; x < w; x += 40) {
        fb_fill_rect(x, 0, 1, h, grid);
    }
    for (y = 40; y < h; y += 40) {
        fb_fill_rect(0, y, w, 1, grid);
    }
    /* The border, one pixel inside the edge. */
    fb_fill_rect(1, 1, w - 2u, 1, white);
    fb_fill_rect(1, h - 2u, w - 2u, 1, white);
    fb_fill_rect(1, 1, 1, h - 2u, white);
    fb_fill_rect(w - 2u, 1, 1, h - 2u, white);
    BIOS_MARK("video_pattern width=%u height=%u", (unsigned)w, (unsigned)h);
    key = shell_key(&ev, VIDEO_MS);
    /* Redraws every cell from the console's buffer. */
    console_set_scheme(console_scheme());
    set(o, POST_OK, "%ux%u pattern shown%s", (unsigned)w, (unsigned)h, key ? ", key" : "");
    BIOS_MARK("diag name=video result=ok width=%u height=%u key=%d", (unsigned)w, (unsigned)h,
              key);
}

/* ---- AUDIO ---- */

static const audio_note_t scale[] = {
    { 262, 120 }, { 294, 120 }, { 330, 120 }, { 349, 120 },
    { 392, 120 }, { 440, 120 }, { 494, 120 }, { 523, 200 },
};
static uint8_t pcm[PCM_LEN];

static void diag_audio(diag_out_t *o)
{
    unsigned i;
    int rc, pcm_rc;

    if (bios.post.item[POST_AUDIO].result != POST_OK) {
        set(o, POST_ABSENT, "no audio");
        BIOS_MARK("diag name=audio result=absent");
        return;
    }
    rc = audio_play(scale, sizeof(scale) / sizeof(scale[0]), AUDIO_PRIORITY);
    audio_wait();
    /* A falling sweep: a square wave whose period grows, 8-bit unsigned. */
    for (i = 0; i < PCM_LEN; i++) {
        unsigned period = 8u + i / 100u;

        pcm[i] = (uint8_t)((i / (period / 2u)) % 2u ? 192 : 64);
    }
    pcm_rc = audio_play_pcm(pcm, PCM_LEN, PCM_RATE, AUDIO_PRIORITY);
    audio_wait();
    if (rc != 0) {
        set(o, POST_FAIL, "%s, scale error %d", audio_backend(), rc);
    } else {
        set(o, POST_OK, "%s, %u notes, PCM %s", audio_backend(),
            (unsigned)(sizeof(scale) / sizeof(scale[0])), pcm_rc == 0 ? "played" : "not supported");
    }
    BIOS_MARK("diag name=audio result=%s backend=%s notes=%u rc=%d pcm=%d",
              rc != 0 ? "fail" : "ok", audio_backend(),
              (unsigned)(sizeof(scale) / sizeof(scale[0])), rc, pcm_rc);
}

/* ---- TIMER ---- */

/*
 * The tick callback stamps the system timer inside the tick interrupt, at
 * the first tick of the window and DIAG_TIMER_TICKS ticks later. Only the
 * difference of the two interrupt latencies stays in the result: the
 * timer's 1 us is 0.5 ppm over the 2 s window, a tick 300 us late is
 * 150 ppm. diagcalc.h derives the limit. The main loop only waits.
 * The first TIMER_SETTLE ticks after the start are not stamped: in QEMU,
 * the serial output of the command line just before can hold the guest
 * back, and the first ticks after it come late.
 *
 * Fault hook for the target tests only, as in post_hw.c:
 *   RETROS_FAULT_TICK   the callback loses every tenth tick interrupt
 */
#define TIMER_SETTLE 5u

static volatile uint32_t tm_left;   /* callbacks still to count; 0: idle */
static volatile uint32_t tm_skip;   /* callbacks to let pass before the first stamp */
static volatile uint32_t tm_seen;
static volatile uint32_t tm_t0, tm_t1, tm_c0, tm_c1;
static int tm_registered;

#ifdef RETROS_FAULT_TICK
static uint32_t fault_tick_drops;
#endif

static void timer_tick(void)
{
    uint32_t now;

    if (tm_left == 0) {
        return;
    }
#ifdef RETROS_FAULT_TICK
    if (++fault_tick_drops % 10u == 0) {
        return;
    }
#endif
    if (tm_skip != 0) {
        tm_skip--;
        return;
    }
    now = timer_now_lo();
    if (tm_seen++ == 0) {
        tm_t0 = now;
        tm_c0 = tick_count();
    }
    if (--tm_left == 0) {
        tm_t1 = now;
        tm_c1 = tick_count();
    }
}

static void diag_timer(diag_out_t *o)
{
    uint32_t hz = tick_hz();
    uint32_t period = tick_period_us_for(hz);
    /* Twice the window and the settle ticks, for a slow tick, and 100 ms for the first one. */
    uint64_t limit = (uint64_t)(DIAG_TIMER_TICKS + TIMER_SETTLE) * period * 2u + 100000u;
    uint64_t t0;
    uint32_t s, us, periods, missed, seen;
    int32_t ppm;

    if (!tm_registered) {
        if (tick_register(timer_tick) != 0) {
            set(o, POST_FAIL, "no free tick callback");
            BIOS_MARK("diag name=timer result=fail reason=callback");
            return;
        }
        tm_registered = 1;
    }
    s = irq_save();
    tm_seen = 0;
    tm_skip = TIMER_SETTLE;
    tm_left = DIAG_TIMER_TICKS + 1u;
    irq_restore(s);
    t0 = timer_now_us();
    while (tm_left != 0 && timer_now_us() - t0 < limit) {
        audio_poll();
    }
    s = irq_save();
    seen = tm_seen;
    tm_left = 0;
    irq_restore(s);
    if (hz == 0 || seen <= DIAG_TIMER_TICKS) {
        set(o, POST_FAIL, "%s", seen == 0 ? "the tick does not run" : "the tick stopped");
        BIOS_MARK("diag name=timer result=fail reason=tick hz=%u seen=%u", (unsigned)hz,
                  (unsigned)seen);
        return;
    }
    us = tm_t1 - tm_t0;
    periods = tm_c1 - tm_c0;
    missed = diag_timer_missed(periods, DIAG_TIMER_TICKS);
    ppm = diag_timer_ppm(us, DIAG_TIMER_TICKS, period);
    set(o, diag_timer_ok(ppm, missed) ? POST_OK : POST_FAIL, "%d ppm over %u ticks, %u missed",
        (int)ppm, (unsigned)DIAG_TIMER_TICKS, (unsigned)missed);
    BIOS_MARK("diag name=timer result=%s ppm=%d ticks=%u periods=%u us=%u period_us=%u missed=%u",
              diag_timer_ok(ppm, missed) ? "ok" : "fail", (int)ppm, (unsigned)DIAG_TIMER_TICKS,
              (unsigned)periods, (unsigned)us, (unsigned)period, (unsigned)missed);
}

/* ---- RNG ---- */

static uint32_t rng_words[RNG_WORDS];

static void diag_rng(diag_out_t *o)
{
    diag_rng_stats_t s;
    int rc = rng_init();

    if (rc == RNG_ERR_BOARD) {
        set(o, POST_ABSENT, "no generator");
        BIOS_MARK("diag name=rng result=absent");
        return;
    }
    if (rc == 0) {
        rc = rng_read(rng_words, sizeof(rng_words));
    }
    if (rc < 0) {
        set(o, POST_FAIL, "read error %d", rc);
        BIOS_MARK("diag name=rng result=fail rc=%d", rc);
        return;
    }
    diag_rng_stats(rng_words, RNG_WORDS, &s);
    set(o, diag_rng_ok(&s) ? POST_OK : POST_FAIL, "%u repeats, %u of %u bits set",
        (unsigned)s.repeats, (unsigned)s.ones, (unsigned)s.bits);
    BIOS_MARK("diag name=rng result=%s words=%u repeats=%u ones=%u bits=%u",
              diag_rng_ok(&s) ? "ok" : "fail", (unsigned)s.words, (unsigned)s.repeats,
              (unsigned)s.ones, (unsigned)s.bits);
}

/* ---- RTC ---- */

static void diag_rtc(diag_out_t *o)
{
    char iso[DT_ISO_SIZE];
    datetime_t dt;
    int rc;

    if (!rtc_present()) {
        set(o, POST_ABSENT, "no clock chip");
        BIOS_MARK("diag name=rtc result=absent");
        return;
    }
    rc = rtc_get(&dt);
    if (rc != 0 || dt_format_iso(&dt, iso, sizeof(iso)) < 0) {
        set(o, POST_FAIL, "read error %d", rc);
        BIOS_MARK("diag name=rtc result=fail rc=%d", rc);
        return;
    }
    set(o, POST_OK, "%s UTC, %s", iso, rtc_chip() == RTC_CHIP_DS3231 ? "DS3231" : "DS1307");
    BIOS_MARK("diag name=rtc result=ok time=%s chip=%s", iso,
              rtc_chip() == RTC_CHIP_DS3231 ? "ds3231" : "ds1307");
}

/* ---- INPUT ---- */

static void diag_input(diag_out_t *o)
{
    char line[BIOS_LINE_MAX];
    input_event_t ev;
    uint64_t end = timer_now_us() + (uint64_t)INPUT_MS * 1000u;
    unsigned n = 0;
    int quit = 0;

    shell_say("Press keys on the console. Q or 10 seconds ends the test.");
    while (timer_now_us() < end) {
        uint32_t left = (uint32_t)((end - timer_now_us()) / 1000u);

        if (!shell_key(&ev, left > 0 ? left : 1u)) {
            continue;
        }
        n++;
        if (ev.type == INPUT_EV_CHAR) {
            k_snprintf(line, sizeof(line), "CHAR 0x%02x %c", (unsigned)ev.code,
                       ev.code >= 0x20 && ev.code < 0x7f ? (char)ev.code : ' ');
        } else {
            const char *name = input_key_name(ev.code);

            k_snprintf(line, sizeof(line), "KEY  %u %s", (unsigned)ev.code,
                       name != NULL ? name : "?");
        }
        shell_say(line);
        if (ev.type == INPUT_EV_CHAR && (ev.code == 'q' || ev.code == 'Q')) {
            quit = 1;
            break;
        }
    }
    set(o, POST_OK, "%u events%s", n, quit ? ", Q" : "");
    BIOS_MARK("diag name=input result=ok events=%u quit=%d", n, quit);
}

/* ---- The table ---- */

static const char *result_text(int r)
{
    switch (r) {
    case POST_OK:      return "OK";
    case POST_FAIL:    return "FAILED";
    case POST_ABSENT:  return "ABSENT";
    default:           return "SKIPPED";
    }
}

static void row(int diag, const diag_out_t *o)
{
    char line[BIOS_LINE_MAX];

    k_snprintf(line, sizeof(line), "  %-8s %-8s%s", sh_diag_name(diag), result_text(o->result),
               o->detail);
    shell_say(line);
}

static void run_one(int diag, const sh_cmd_t *c, int all, diag_out_t *o)
{
    o->result = POST_SKIPPED;
    o->detail[0] = '\0';
    switch (diag) {
    case SH_DIAG_MEM:   diag_mem(c, all, o); break;
    case SH_DIAG_SD:    diag_sd(c, o); break;
    case SH_DIAG_VIDEO: diag_video(o); break;
    case SH_DIAG_AUDIO: diag_audio(o); break;
    case SH_DIAG_TIMER: diag_timer(o); break;
    case SH_DIAG_RNG:   diag_rng(o); break;
    case SH_DIAG_RTC:   diag_rtc(o); break;
    case SH_DIAG_INPUT: diag_input(o); break;
    default: break;
    }
}

int diag_command(const sh_cmd_t *c)
{
    char line[BIOS_LINE_MAX];
    diag_out_t o;
    unsigned ok = 0, fail = 0, absent = 0, skipped = 0;
    int d;

    if (c->diag == SH_DIAG_LIST) {
        shell_say("Tests. DIAG <test> runs one, DIAG ALL runs all but INPUT.");
        for (d = 0; d <= SH_DIAG_ALL; d++) {
            k_snprintf(line, sizeof(line), "  %s", sh_diag_summary(d));
            shell_say(line);
        }
        return 0;
    }
    if (c->diag != SH_DIAG_ALL) {
        run_one(c->diag, c, 0, &o);
        row(c->diag, &o);
        return o.result == POST_FAIL ? -1 : 0;
    }
    shell_say("DIAGNOSTICS");
    for (d = 0; d < SH_DIAG_COUNT; d++) {
        if (d == SH_DIAG_INPUT) {
            continue;
        }
        run_one(d, NULL, 1, &o);
        row(d, &o);
        switch (o.result) {
        case POST_OK:     ok++; break;
        case POST_FAIL:   fail++; break;
        case POST_ABSENT: absent++; break;
        default:          skipped++; break;
        }
    }
    k_snprintf(line, sizeof(line), "DIAG: %u OK, %u FAILED, %u ABSENT, %u SKIPPED", ok, fail,
               absent, skipped);
    shell_say(line);
    BIOS_MARK("diag_done ok=%u fail=%u absent=%u skipped=%u", ok, fail, absent, skipped);
    return fail != 0 ? -1 : 0;
}
