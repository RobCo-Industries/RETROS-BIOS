/* The POST probes. They fill bios.post; post.c holds the pure logic. */

#include <stdarg.h>
#include <stdint.h>

#include <robco/audio.h>
#include <robco/blk.h>
#include <robco/datetime.h>
#include <robco/fb.h>
#include <robco/fs.h>
#include <robco/i2c.h>
#include <robco/klibc.h>
#include <robco/part.h>
#include <robco/rng.h>
#include <robco/rtc.h>
#include <robco/sd.h>
#include <robco/timer.h>

#include "bios.h"
#include "boot.h"
#include "bootvol.h"
#include "post.h"

/* Polls of the system timer before the timer probe gives up. */
#define TIMER_POLLS 1000000u

/*
 * Fault hooks for the target tests only. tests/ builds a separate image
 * under build/ with one of these defined; the shipped image defines none.
 *   RETROS_FAULT_TIMER    the timer probe reads a counter that never moves
 *   RETROS_FAULT_TIMER_LATE  the same, from the second POST run on: a shell POST
 *   RETROS_FAULT_MEMTEST  the memory test reads bit 7 stuck at 1 in one word
 *   RETROS_FAULT_MEMTEST_LATE  the same, from the second POST run on: a shell POST
 *   RETROS_FAULT_TICK     DIAG TIMER loses every tenth tick, see diag.c
 *   RETROS_FAULT_PCM      DIAG AUDIO asks for a bad PCM rate, see diag.c
 * A fault image prints a fault_hook marker before the first probe.
 */
#if defined(RETROS_FAULT_TIMER_LATE) || defined(RETROS_FAULT_MEMTEST_LATE)
/* POST runs so far. The boot's run passes; a shell POST finds the fault. */
static unsigned fault_post_runs;
#endif

#ifdef RETROS_FAULT_TIMER
static uint32_t probe_timer_now(void)
{
    return 0x00c0ffeeu;
}
#elif defined(RETROS_FAULT_TIMER_LATE)
static uint32_t probe_timer_now(void)
{
    return fault_post_runs > 1u ? 0x00c0ffeeu : timer_now_lo();
}
#else
static uint32_t probe_timer_now(void)
{
    return timer_now_lo();
}
#endif

#if defined(RETROS_FAULT_MEMTEST) || defined(RETROS_FAULT_MEMTEST_LATE)
static uint32_t stuck_bit7(volatile uint32_t *p, uint32_t v)
{
    return p == (volatile uint32_t *)(POST_MEM_BASE + 0x100u) ? v | 0x80u : v;
}
#endif

static uint8_t block0[512] __attribute__((aligned(64)));

static void set(unsigned probe, int result, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void set(unsigned probe, int result, const char *fmt, ...)
{
    post_item_t *it = &bios.post.item[probe];
    va_list ap;

    it->result = result;
    va_start(ap, fmt);
    k_vsnprintf(it->detail, sizeof(it->detail), fmt, ap);
    va_end(ap);
}

static void mark(unsigned probe, const char *extra)
{
    BIOS_MARK("post name=%s result=%s%s%s", post_name(probe),
              post_result_name(bios.post.item[probe].result), extra[0] != '\0' ? " " : "", extra);
}

static void probe_memory(void)
{
    memtest_fault_t f;
    char extra[96];
    uint64_t t0;
    int rc;

#ifdef RETROS_FAULT_MEMTEST
    memtest_read_hook = stuck_bit7;
#elif defined(RETROS_FAULT_MEMTEST_LATE)
    memtest_read_hook = fault_post_runs > 1u ? stuck_bit7 : 0;
#endif
    t0 = timer_now_us();
    rc = memtest_run((volatile uint32_t *)POST_MEM_BASE, POST_MEM_SIZE / 4u, &f);
    uint32_t us = (uint32_t)(timer_now_us() - t0);

    if (rc == 0) {
        set(POST_MEMORY, POST_OK, "%u KiB at 0x%08x", (unsigned)(POST_MEM_SIZE / 1024u),
            (unsigned)POST_MEM_BASE);
        k_snprintf(extra, sizeof(extra), "base=0x%08x size=%u us=%u", (unsigned)POST_MEM_BASE,
                   (unsigned)POST_MEM_SIZE, (unsigned)us);
    } else {
        set(POST_MEMORY, POST_FAIL, "%s at 0x%08x", f.step, (unsigned)f.addr);
        k_snprintf(extra, sizeof(extra), "step=%s addr=0x%08x expected=0x%08x actual=0x%08x",
                   f.step, (unsigned)f.addr, (unsigned)f.expected, (unsigned)f.actual);
    }
    mark(POST_MEMORY, extra);
}

static void probe_video(void)
{
    const fb_info_t *fb = fb_get();
    char extra[48];

    if (bios.fb_ok) {
        set(POST_VIDEO, POST_OK, "%ux%ux%u", (unsigned)fb->width, (unsigned)fb->height,
            (unsigned)fb->depth);
        k_snprintf(extra, sizeof(extra), "addr=0x%08x", (unsigned)(uintptr_t)fb->addr);
    } else {
        set(POST_VIDEO, POST_FAIL, "no framebuffer");
        extra[0] = '\0';
    }
    mark(POST_VIDEO, extra);
}

static void probe_sd(void)
{
    char extra[24];
    int rc = sd_init();

    if (rc == SD_ERR_NOCARD) {
        set(POST_SD, POST_ABSENT, "no card");
    } else if (rc != 0) {
        set(POST_SD, POST_FAIL, "init error %d", rc);
    } else if ((rc = blk_read(sd_blkdev(), 0, 1, block0)) != 0) {
        set(POST_SD, POST_FAIL, "block 0 read error %d", rc);
    } else {
        set(POST_SD, POST_OK, "block 0 read");
    }
    k_snprintf(extra, sizeof(extra), "rc=%d", rc);
    mark(POST_SD, extra);
}

static int unmount_sd(void)
{
    return fs_unmount("sd");
}

/* The boot volume markers, in the order the load tries them. */
static const char *const boot_markers[] = { BIOS_PRIMARY, BIOS_BACKUP };

/* Nonzero when part_scan() finds no entry: no table and no FAT boot sector. */
static int card_empty(void)
{
    part_info_t parts[PART_MAX];

    return part_scan(sd_blkdev(), parts, PART_MAX) == 0;
}

/*
 * Mounts the boot volume with RobCo-Core's fs_mount_boot() and the BIOS
 * markers: the first volume that holds MFBOOT.SYS or MFBOOT.BAK as a
 * non-empty regular file, else the first volume that mounts, so that the
 * load reports the missing file. bootvol.c names the failures.
 *
 * On a POST rerun the volume of the earlier run goes first. Its unmount
 * syncs the card. A card that is gone lets the unmount through. Any
 * other sync failure keeps the volume registered under "sd", so the
 * probe reports the error and mounts nothing. The SD probe ran just
 * before, so a card that answers again lets the unmount through.
 */
static void probe_fat(void)
{
    char extra[48];
    char detail[POST_DETAIL_MAX];
    char where[16];
    const char *what;
    int rc;

    rc = bootvol_release(&bios.mounted, unmount_sd);
    if (rc != 0) {
        set(POST_FAT, POST_FAIL, "old volume still mounted, error %d", rc);
        k_snprintf(extra, sizeof(extra), "what=held rc=%d", rc);
        mark(POST_FAT, extra);
        return;
    }
    if (bios.post.item[POST_SD].result != POST_OK) {
        set(POST_FAT, POST_ABSENT, "no card");
        mark(POST_FAT, "");
        return;
    }
    rc = fs_mount_boot("sd", sd_blkdev(), boot_markers, 2, &bios.vol);
    if (rc != PART_BOOT_OK) {
        /* A volume that did not unmount stays registered under "sd". */
        if (rc == PART_BOOT_ERR_RELEASE) {
            bios.mounted = 1;
        }
        what = bootvol_fail(rc, bios.vol.last_err,
                            rc == PART_BOOT_ERR_NOFAT && card_empty(), detail, sizeof(detail));
        set(POST_FAT, POST_FAIL, "%s", detail);
        k_snprintf(extra, sizeof(extra), "what=%s rc=%d", what, bios.vol.last_err);
        mark(POST_FAT, extra);
        return;
    }
    bios.mounted = 1;
    if (bios.vol.slot == FS_BOOT_WHOLE) {
        k_snprintf(where, sizeof(where), "whole");
        set(POST_FAT, POST_OK, "whole card, %u blocks", (unsigned)bios.vol.block_count);
    } else {
        k_snprintf(where, sizeof(where), "%u", (unsigned)bios.vol.slot);
        set(POST_FAT, POST_OK, "part %u, LBA %u, %u blocks", (unsigned)bios.vol.slot,
            (unsigned)bios.vol.start_lba, (unsigned)bios.vol.block_count);
    }
    mark(POST_FAT, "");
    BIOS_MARK("mount part=%s lba=%u blocks=%u", where, (unsigned)bios.vol.start_lba,
              (unsigned)bios.vol.block_count);
}

static void probe_timer(void)
{
    uint32_t t0 = probe_timer_now();
    uint32_t i;

    for (i = 0; i < TIMER_POLLS; i++) {
        if (probe_timer_now() != t0) {
            break;
        }
    }
    if (i < TIMER_POLLS) {
        set(POST_TIMER, POST_OK, "advancing");
    } else {
        set(POST_TIMER, POST_FAIL, "stopped");
    }
    mark(POST_TIMER, "");
}

static void probe_rng(void)
{
    uint32_t v;
    char extra[64];
    uint64_t t0 = timer_now_us();
    uint64_t t1;
    int rc = rng_init();

    /* The read waits for what is left of the generator's warm-up. */
    if (rc == 0) {
        rc = rng_u32(&v);
    }
    t1 = timer_now_us();
    if (rc == RNG_ERR_BOARD) {
        set(POST_RNG, POST_ABSENT, "%s", "");
    } else if (rc != 0) {
        set(POST_RNG, POST_FAIL, "error %d", rc);
    } else {
        set(POST_RNG, POST_OK, "hardware");
    }
    k_snprintf(extra, sizeof(extra), "rc=%d us=%u since_us=%u", rc, (unsigned)(t1 - t0),
               (unsigned)(t1 - bios.rng_t0));
    mark(POST_RNG, extra);
}

static void probe_rtc(void)
{
    datetime_t dt;
    char extra[16];
    int rc = i2c_init(100000);

    if (rc == 0) {
        rc = rtc_init();
    }
    if (!rtc_present()) {
        set(POST_RTC, POST_ABSENT, "no clock chip");
    } else if ((rc = rtc_get(&dt)) != 0) {
        set(POST_RTC, POST_FAIL, "read error %d", rc);
    } else {
        set(POST_RTC, POST_OK, "%s", rtc_chip() == RTC_CHIP_DS3231 ? "DS3231" : "DS1307");
    }
    k_snprintf(extra, sizeof(extra), "rc=%d", rc);
    mark(POST_RTC, extra);
}

static void probe_audio(void)
{
    char extra[48];
    int rc = audio_init();

    if (rc == 0) {
        set(POST_AUDIO, POST_OK, "%s", audio_backend());
    } else if (rc == AUDIO_ERR_NO_PWM || rc == AUDIO_ERR_PIN) {
        set(POST_AUDIO, POST_ABSENT, "no PWM output");
    } else {
        set(POST_AUDIO, POST_FAIL, "error %d", rc);
    }
    k_snprintf(extra, sizeof(extra), "rc=%d backend=%s probe=%s", rc, audio_backend(),
               audio_probe_name(audio_probe_result()));
    mark(POST_AUDIO, extra);
}

/* In table order: the index of each probe is its POST_* number. */
static void (*const probes[POST_COUNT])(void) = {
    probe_memory, probe_video, probe_timer, probe_sd, probe_fat, probe_rng, probe_rtc,
    probe_audio,
};

void post_run(void)
{
    unsigned i;
    unsigned stop = POST_COUNT;

    post_table_init(&bios.post);
#ifdef RETROS_FAULT_TIMER
    BIOS_MARK("fault_hook name=timer");
#endif
#ifdef RETROS_FAULT_TIMER_LATE
    fault_post_runs++;
    BIOS_MARK("fault_hook name=timer_late");
#endif
#ifdef RETROS_FAULT_MEMTEST
    BIOS_MARK("fault_hook name=memtest");
#endif
#ifdef RETROS_FAULT_MEMTEST_LATE
    fault_post_runs++;
    BIOS_MARK("fault_hook name=memtest_late");
#endif
#ifdef RETROS_FAULT_TICK
    BIOS_MARK("fault_hook name=tick");
#endif
#ifdef RETROS_FAULT_PCM
    BIOS_MARK("fault_hook name=pcm");
#endif
    for (i = 0; i < POST_COUNT; i++) {
        if (stop < POST_COUNT && !post_runs_after(stop, i)) {
            /* The table already says skipped. */
            char extra[24];

            k_snprintf(extra, sizeof(extra), "after=%s", post_name(stop));
            mark(i, extra);
            continue;
        }
        probes[i]();
        if (stop == POST_COUNT && post_stops(&bios.post, i)) {
            stop = i;
        }
    }
}
