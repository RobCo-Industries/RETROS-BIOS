/*
 * The decisions of the diagnostics: the default memory window, the
 * memory patterns and their checks, the RNG statistics, the timer
 * comparison and the SD speed. Pure code: it builds and runs on the host
 * in the unit tests. diag.c runs the tests on the hardware.
 */
#ifndef RETROS_DIAGCALC_H
#define RETROS_DIAGCALC_H

#include <stddef.h>
#include <stdint.h>

/*
 * The lowest address DIAG MEM may write: the end of the MFBootAgent slot.
 * Below it are the firmware area, the BIOS image with its stacks, the
 * handoff area and the MFBootAgent slot. See MEMORY_MAP.md.
 */
#define DIAG_MEM_LOW    0x00400000u
#define DIAG_MIB        0x00100000u

/*
 * The default DIAG MEM window: from DIAG_MEM_LOW to the end of ARM RAM,
 * mem_base + mem_size, cut down to whole MiB. When the framebuffer
 * [fb_addr, fb_addr + fb_size) lies inside that range, the larger of the
 * two parts on either side of it is taken. On the Pi the framebuffer is
 * GPU memory, above ARM RAM, so that case is a guard. Returns 0 and sets
 * *base and *size, or -1 when no whole MiB is left.
 */
int diag_mem_window(uint32_t mem_base, uint32_t mem_size, uint32_t fb_addr, uint32_t fb_size,
                    uint32_t *base, uint32_t *size);

/*
 * Nonzero when [addr, addr + len) lies inside the default window rules:
 * at or above DIAG_MEM_LOW, inside ARM RAM and clear of the framebuffer.
 */
int diag_mem_allowed(uint32_t addr, uint32_t len, uint32_t mem_base, uint32_t mem_size,
                     uint32_t fb_addr, uint32_t fb_size);

/* Patterns, in the order DIAG MEM runs them. */
enum {
    DIAG_PAT_WALK,      /* walking ones: 32 values in the first word of every 4 KiB */
    DIAG_PAT_ADDR,      /* each word holds its own address */
    DIAG_PAT_CHECK,     /* 0x55555555 and 0xaaaaaaaa in alternate words */
    DIAG_PAT_ICHECK,    /* the complement of DIAG_PAT_CHECK */
    DIAG_PAT_COUNT
};

/* Marker name of a pattern: "walk", "addr", "check", "icheck". */
const char *diag_pat_name(int pat);

/* The value a fill pass writes at address `addr` for DIAG_PAT_ADDR, CHECK or ICHECK. */
uint32_t diag_pat_value(int pat, uint32_t addr);

typedef struct {
    uint32_t addr;
    uint32_t expected;
    uint32_t actual;
    int pat;            /* DIAG_PAT_* */
} diag_mem_fault_t;

/* Writes the pattern into `words` words at p. Not for DIAG_PAT_WALK. */
void diag_mem_fill(volatile uint32_t *p, uint32_t words, int pat);

/*
 * Reads back what diag_mem_fill() wrote. Returns 0, or -1 with *f set to
 * the first word that differs.
 */
int diag_mem_check(volatile uint32_t *p, uint32_t words, int pat, diag_mem_fault_t *f);

/*
 * Walking ones in the first word of every 4 KiB of `words` words at p:
 * each of the 32 single-bit values is written and read back. Returns 0
 * or -1 with *f set.
 */
int diag_mem_walk(volatile uint32_t *p, uint32_t words, diag_mem_fault_t *f);

/* RNG statistics over n words. */
typedef struct {
    uint32_t words;
    uint32_t repeats;   /* words equal to the one before */
    uint32_t ones;      /* set bits over all words */
    uint32_t bits;      /* 32 * words */
} diag_rng_stats_t;

void diag_rng_stats(const uint32_t *w, uint32_t n, diag_rng_stats_t *s);

/*
 * The sanity rule: no repeat, and the count of ones within 512 of half
 * the bits. For 32768 bits the standard deviation is about 90.5, so 512
 * is more than 5.6 deviations: a working generator fails it about once
 * in 50 million runs. Not a randomness certification.
 */
#define DIAG_RNG_SLACK  512u
int diag_rng_ok(const diag_rng_stats_t *s);

/*
 * The timer test. The tick callback stamps the system timer at the first
 * tick of the window and again DIAG_TIMER_TICKS ticks later. Both stamps
 * lie on the tick's period grid, each late by its own interrupt latency,
 * so only the difference of the two latencies enters the result. The
 * timer counts microseconds: 1 us over the 2 s window is 0.5 ppm, and a
 * tick 300 us late moves the result by 150 ppm.
 */
#define DIAG_TIMER_TICKS 200u

/*
 * Over one measurement, `timer_us` microseconds passed on the system
 * timer and `ticks` tick periods of `period_us` each. Returns the tick
 * time minus the timer time, in parts per million of the timer time (0
 * when timer_us is 0), clamped to +-1000000.
 */
int32_t diag_timer_ppm(uint64_t timer_us, uint32_t ticks, uint32_t period_us);

/*
 * The pass limit. A tick interrupt that comes a whole period late makes
 * the library count a missed tick. So with no missed tick, the two
 * latencies differ by less than one period, and the result stays under
 * one period over the window: 1000000 / DIAG_TIMER_TICKS = 5000 ppm.
 * A tick 1% fast or slow gives 10000 ppm, twice the limit.
 */
#define DIAG_TIMER_PPM_MAX ((int32_t)(1000000u / DIAG_TIMER_TICKS))

/*
 * Ticks the callback did not see: `periods` tick periods the library
 * counted over the window, minus the `ticks` callbacks. 0 when periods
 * is not above ticks.
 */
uint32_t diag_timer_missed(uint32_t periods, uint32_t ticks);

/* The test passes: within DIAG_TIMER_PPM_MAX and no missed tick. */
int diag_timer_ok(int32_t ppm, uint32_t missed);

/*
 * The audio test, once POST found a backend. Both backends, pwm and
 * trace, play PCM: audio.h has no "unsupported" result, and every error
 * code of audio_play() and audio_play_pcm() is a failure. scale_rc and
 * pcm_rc are the two return codes. Returns one of these:
 */
enum {
    DIAG_AUDIO_OK,          /* the scale and the PCM both played */
    DIAG_AUDIO_FAIL_SCALE,  /* audio_play() failed, the PCM played */
    DIAG_AUDIO_FAIL_PCM,    /* the scale played, audio_play_pcm() failed */
    DIAG_AUDIO_FAIL_BOTH    /* both calls failed */
};
int diag_audio_result(int scale_rc, int pcm_rc);

/* Marker name of the failed step: "none", "scale", "pcm", "both". */
const char *diag_audio_step(int result);

/* KiB per second for `bytes` read in `us` microseconds, rounded down. 0 when us is 0. */
uint32_t diag_kib_per_s(uint64_t bytes, uint64_t us);

#endif
