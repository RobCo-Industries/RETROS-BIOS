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
 *   RETROS_FAULT_MEMTEST  the memory test reads bit 7 stuck at 1 in one word
 * A fault image prints a fault_hook marker before the first probe.
 */
#ifdef RETROS_FAULT_TIMER
static uint32_t probe_timer_now(void)
{
    return 0x00c0ffeeu;
}
#else
static uint32_t probe_timer_now(void)
{
    return timer_now_lo();
}
#endif

#ifdef RETROS_FAULT_MEMTEST
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

static int mount_entry(const part_info_t *p)
{
    int rc;

    bios.part = *p;
    rc = part_open(sd_blkdev(), &bios.part, &bios.part_dev);
    if (rc == 0) {
        rc = fs_mount("sd", &bios.part_dev);
    }
    return rc;
}

static int has_boot_file(void)
{
    fs_stat_t st;

    return fs_stat("sd:" BIOS_PRIMARY, &st) == 0 || fs_stat("sd:" BIOS_BACKUP, &st) == 0;
}

/*
 * Mounts the boot volume: the first FAT entry, in slot order, that mounts
 * and holds MFBOOT.SYS or MFBOOT.BAK. When FAT entries mount but none
 * holds them, the first of these stays mounted and the load reports the
 * missing file. bootvol.c makes the choice; this function only probes.
 */
static void probe_fat(void)
{
    part_info_t parts[PART_MAX];
    int state[PART_MAX];
    char extra[48];
    char where[16];
    int rc = FS_ERR_NOFS;
    int n, i, pick, gpt = 0, fat = 0;

    if (bios.post.item[POST_SD].result != POST_OK) {
        set(POST_FAT, POST_ABSENT, "no card");
        mark(POST_FAT, "");
        return;
    }
    n = part_scan(sd_blkdev(), parts, PART_MAX);
    for (i = 0; i < n; i++) {
        state[i] = BOOTVOL_NOT_FAT;
        if (bootvol_is_gpt(&parts[i])) {
            gpt = 1;
        }
        if (!bootvol_is_fat(&parts[i])) {
            continue;
        }
        fat = 1;
        rc = mount_entry(&parts[i]);
        if (rc != 0) {
            state[i] = BOOTVOL_NO_MOUNT;
            continue;
        }
        state[i] = has_boot_file() ? BOOTVOL_BOOT : BOOTVOL_EMPTY;
        fs_unmount("sd");
        if (state[i] == BOOTVOL_BOOT) {
            n = i + 1; /* bootvol_pick() takes the first one */
            break;
        }
    }
    pick = n > 0 ? bootvol_pick(state, (unsigned)n) : -1;
    if (pick < 0) {
        if (gpt && !fat) {
            set(POST_FAT, POST_FAIL, "GPT card, not supported");
            k_snprintf(extra, sizeof(extra), "what=gpt rc=%d", n);
        } else if (n < 1) {
            set(POST_FAT, POST_FAIL, "no partition");
            k_snprintf(extra, sizeof(extra), "what=part rc=%d", n);
        } else if (!fat) {
            set(POST_FAT, POST_FAIL, "no FAT partition");
            k_snprintf(extra, sizeof(extra), "what=nofat rc=%d", n);
        } else {
            set(POST_FAT, POST_FAIL, "no FAT volume mounts");
            k_snprintf(extra, sizeof(extra), "what=mount rc=%d", rc);
        }
        mark(POST_FAT, extra);
        return;
    }
    rc = mount_entry(&parts[pick]);
    if (rc != 0) {
        set(POST_FAT, POST_FAIL, "mount error %d", rc);
        k_snprintf(extra, sizeof(extra), "what=mount rc=%d", rc);
        mark(POST_FAT, extra);
        return;
    }
    bios.mounted = 1;
    if (bios.part.superfloppy) {
        k_snprintf(where, sizeof(where), "whole");
        set(POST_FAT, POST_OK, "whole card, %u blocks", (unsigned)bios.part.block_count);
    } else {
        k_snprintf(where, sizeof(where), "%u", (unsigned)bios.part.index);
        set(POST_FAT, POST_OK, "part %u, LBA %u, %u blocks", (unsigned)bios.part.index,
            (unsigned)bios.part.start_lba, (unsigned)bios.part.block_count);
    }
    mark(POST_FAT, "");
    BIOS_MARK("mount part=%s lba=%u blocks=%u", where, (unsigned)bios.part.start_lba,
              (unsigned)bios.part.block_count);
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
    char extra[16];
    int rc = rng_init();

    if (rc == RNG_ERR_BOARD) {
        set(POST_RNG, POST_ABSENT, "%s", "");
    } else if (rc != 0 || (rc = rng_u32(&v)) != 0) {
        set(POST_RNG, POST_FAIL, "error %d", rc);
    } else {
        set(POST_RNG, POST_OK, "hardware");
    }
    k_snprintf(extra, sizeof(extra), "rc=%d", rc);
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
    probe_memory, probe_timer, probe_video, probe_sd, probe_fat, probe_rng, probe_rtc,
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
#ifdef RETROS_FAULT_MEMTEST
    BIOS_MARK("fault_hook name=memtest");
#endif
    for (i = 0; i < POST_COUNT; i++) {
        if (stop < POST_COUNT) {
            /* The table already says skipped. */
            char extra[24];

            k_snprintf(extra, sizeof(extra), "after=%s", post_name(stop));
            mark(i, extra);
            continue;
        }
        probes[i]();
        if (post_stops(&bios.post, i)) {
            stop = i;
        }
    }
}
