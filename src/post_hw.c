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
#include "post.h"

/* Polls of the system timer before the timer probe gives up. */
#define TIMER_POLLS 1000000u

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
    uint64_t t0 = timer_now_us();
    int rc = memtest_run((volatile uint32_t *)POST_MEM_BASE, POST_MEM_SIZE / 4u, &f);
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

static void probe_fat(void)
{
    char extra[48];
    int rc = 0;
    int n;

    if (bios.post.item[POST_SD].result != POST_OK) {
        set(POST_FAT, POST_ABSENT, "no card");
        mark(POST_FAT, "");
        return;
    }
    n = part_scan(sd_blkdev(), &bios.part, 1);
    if (n < 1) {
        set(POST_FAT, POST_FAIL, "no partition");
        k_snprintf(extra, sizeof(extra), "what=part rc=%d", n);
        mark(POST_FAT, extra);
        return;
    }
    rc = part_open(sd_blkdev(), &bios.part, &bios.part_dev);
    if (rc == 0) {
        rc = fs_mount("sd", &bios.part_dev);
    }
    if (rc != 0) {
        set(POST_FAT, POST_FAIL, "mount error %d", rc);
        k_snprintf(extra, sizeof(extra), "what=mount rc=%d", rc);
        mark(POST_FAT, extra);
        return;
    }
    bios.mounted = 1;
    set(POST_FAT, POST_OK, "LBA %u, %u blocks", (unsigned)bios.part.start_lba,
        (unsigned)bios.part.block_count);
    mark(POST_FAT, "");
    BIOS_MARK("mount lba=%u blocks=%u", (unsigned)bios.part.start_lba,
              (unsigned)bios.part.block_count);
}

static void probe_timer(void)
{
    uint32_t t0 = timer_now_lo();
    uint32_t i;

    for (i = 0; i < TIMER_POLLS; i++) {
        if (timer_now_lo() != t0) {
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

void post_run(void)
{
    post_table_init(&bios.post);
    probe_memory();
    probe_video();
    probe_sd();
    probe_fat();
    probe_timer();
    probe_rng();
    probe_rtc();
    probe_audio();
}
