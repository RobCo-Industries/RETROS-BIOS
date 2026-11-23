/* Diagnostics logic: the memory window per board, the patterns with injected faults, RNG and timer statistics. */

#include <string.h>

#include "diagcalc.h"
#include "post.h"
#include "rc_test.h"

#define MIB 0x00100000u

TEST(window_per_board)
{
    uint32_t base = 0, size = 0;

    /* ARM memory the QEMU boards report: 512 MiB boards and 1 GiB boards, 64 MiB to the GPU. */
    CHECK_EQ(diag_mem_window(0, 448 * MIB, 0x1c000000u, 0x12c000u, &base, &size), 0);
    CHECK_EQ(base, 0x00400000u);
    CHECK_EQ(size, 444 * MIB);
    CHECK_EQ(diag_mem_window(0, 960 * MIB, 0x3c000000u, 0x12c000u, &base, &size), 0);
    CHECK_EQ(size, 956 * MIB);
    /* Real boards: 256 MiB with gpu_mem=64, 512 MiB, 1 GiB with 76 MiB to the GPU. */
    CHECK_EQ(diag_mem_window(0, 192 * MIB, 0, 0, &base, &size), 0);
    CHECK_EQ(size, 188 * MIB);
    CHECK_EQ(diag_mem_window(0, 512 * MIB - 1, 0, 0, &base, &size), 0);
    CHECK_EQ(size, 507 * MIB);
    CHECK_EQ(diag_mem_window(0, 948 * MIB, 0, 0, &base, &size), 0);
    CHECK_EQ(base + size, 948 * MIB);
}

TEST(window_avoids_framebuffer)
{
    uint32_t base = 0, size = 0;

    /* A framebuffer inside ARM RAM near the top: the part below wins. */
    CHECK_EQ(diag_mem_window(0, 64 * MIB, 60 * MIB + 0x1000u, 0x12c000u, &base, &size), 0);
    CHECK_EQ(base, 4 * MIB);
    CHECK_EQ(size, 56 * MIB);
    /* Near the bottom: the part above wins, starting at the next whole MiB. */
    CHECK_EQ(diag_mem_window(0, 64 * MIB, 5 * MIB, 0x12c000u, &base, &size), 0);
    CHECK_EQ(base, 7 * MIB);
    CHECK_EQ(size, 57 * MIB);
    CHECK(diag_mem_allowed(base, size, 0, 64 * MIB, 5 * MIB, 0x12c000u));
    CHECK(!diag_mem_allowed(5 * MIB, 4, 0, 64 * MIB, 5 * MIB, 0x12c000u));
}

TEST(window_too_small)
{
    uint32_t base = 1, size = 1;

    CHECK_EQ(diag_mem_window(0, 4 * MIB, 0, 0, &base, &size), -1);
    CHECK_EQ(diag_mem_window(0, 5 * MIB - 1, 0, 0, &base, &size), -1);
    CHECK_EQ(base, 1);
    CHECK_EQ(diag_mem_window(0, 5 * MIB, 0, 0, &base, &size), 0);
    CHECK_EQ(size, MIB);
}

TEST(allowed)
{
    CHECK(diag_mem_allowed(0x00400000u, 4, 0, 448 * MIB, 0, 0));
    CHECK(!diag_mem_allowed(0x003ffffcu, 8, 0, 448 * MIB, 0, 0));
    CHECK(!diag_mem_allowed(0x00008000u, 4, 0, 448 * MIB, 0, 0));
    CHECK(!diag_mem_allowed(448 * MIB - 4, 8, 0, 448 * MIB, 0, 0));
    CHECK(diag_mem_allowed(448 * MIB - 4, 4, 0, 448 * MIB, 0, 0));
    CHECK(!diag_mem_allowed(0xfffffffcu, 8, 0, 0xffffffffu, 0, 0));
    CHECK(!diag_mem_allowed(0x00400000u, 0, 0, 448 * MIB, 0, 0));
}

#define WORDS 8192u
static uint32_t mem[WORDS];
static volatile uint32_t *stuck_at;
static uint32_t stuck_mask;
static uint32_t stuck_zero;

static uint32_t stuck(volatile uint32_t *p, uint32_t v)
{
    return p == stuck_at ? (v | stuck_mask) & ~stuck_zero : v;
}

/* Address bit `alias` (in words) ignored: reads see the lower twin. */
static uint32_t alias;
static uint32_t shorted(volatile uint32_t *p, uint32_t v)
{
    size_t i = (size_t)(p - (volatile uint32_t *)mem);

    (void)v;
    return mem[i & ~(size_t)alias];
}

static int run_all(diag_mem_fault_t *f)
{
    int pat;

    if (diag_mem_walk(mem, WORDS, f) != 0) {
        return -1;
    }
    for (pat = DIAG_PAT_ADDR; pat < DIAG_PAT_COUNT; pat++) {
        diag_mem_fill(mem, WORDS, pat);
        if (diag_mem_check(mem, WORDS, pat, f) != 0) {
            return -1;
        }
    }
    return 0;
}

TEST(patterns_pass_on_good_memory)
{
    diag_mem_fault_t f;

    memtest_read_hook = NULL;
    CHECK_EQ(run_all(&f), 0);
    CHECK_EQ(mem[0], ~0x55555555u);
}

TEST(stuck_bit_found_by_addr)
{
    diag_mem_fault_t f;
    /* The word's own address must have bit 7 clear, or the pattern hides the stuck bit. */
    unsigned i = ((uintptr_t)&mem[0x40] & 0x80u) != 0 ? 0x60u : 0x40u;

    stuck_at = &mem[i];
    stuck_mask = 1u << 7;
    stuck_zero = 0;
    memtest_read_hook = stuck;
    CHECK_EQ(run_all(&f), -1);
    memtest_read_hook = NULL;
    CHECK_EQ(f.pat, DIAG_PAT_ADDR);
    CHECK_EQ(f.addr, (uint32_t)(uintptr_t)&mem[i]);
    CHECK_EQ(f.actual, f.expected | 0x80u);
    CHECK_STR(diag_pat_name(f.pat), "addr");
}

TEST(stuck_bit_found_by_walk)
{
    diag_mem_fault_t f;

    stuck_at = &mem[1024];
    stuck_mask = 0;
    stuck_zero = 1u << 31;
    memtest_read_hook = stuck;
    CHECK_EQ(run_all(&f), -1);
    memtest_read_hook = NULL;
    CHECK_EQ(f.pat, DIAG_PAT_WALK);
    CHECK_EQ(f.expected, 1u << 31);
    CHECK_EQ(f.actual, 0);
}

TEST(stuck_low_bit_found_by_checkerboard)
{
    diag_mem_fault_t f;
    int pat;

    /* Bit 1 stuck at 1: the checkerboard word there has bit 1 set, its complement has it clear. */
    stuck_at = &mem[3];
    stuck_mask = 1u << 1;
    stuck_zero = 0;
    memtest_read_hook = stuck;
    /* Skip the walk: start at the checkerboard. */
    for (pat = DIAG_PAT_CHECK; pat < DIAG_PAT_COUNT; pat++) {
        diag_mem_fill(mem, WORDS, pat);
        if (diag_mem_check(mem, WORDS, pat, &f) != 0) {
            break;
        }
    }
    memtest_read_hook = NULL;
    CHECK(pat < DIAG_PAT_COUNT);
    CHECK_EQ(f.addr, (uint32_t)(uintptr_t)&mem[3]);
    CHECK_EQ(f.actual & 2u, 2u);
}

TEST(shorted_address_line_found)
{
    diag_mem_fault_t f;

    alias = 256;
    memtest_read_hook = shorted;
    CHECK_EQ(run_all(&f), -1);
    memtest_read_hook = NULL;
    CHECK_EQ(f.pat, DIAG_PAT_ADDR);
    CHECK_EQ(f.addr, (uint32_t)(uintptr_t)&mem[256]);
}

TEST(pattern_values)
{
    CHECK_EQ(diag_pat_value(DIAG_PAT_ADDR, 0x1234u), 0x1234u);
    CHECK_EQ(diag_pat_value(DIAG_PAT_CHECK, 0x1000u), 0x55555555u);
    CHECK_EQ(diag_pat_value(DIAG_PAT_CHECK, 0x1004u), 0xaaaaaaaau);
    CHECK_EQ(diag_pat_value(DIAG_PAT_ICHECK, 0x1000u), 0xaaaaaaaau);
    CHECK_STR(diag_pat_name(DIAG_PAT_WALK), "walk");
    CHECK_STR(diag_pat_name(DIAG_PAT_ICHECK), "icheck");
    CHECK_STR(diag_pat_name(9), "unknown");
}

TEST(rng_stats)
{
    static uint32_t w[1024];
    diag_rng_stats_t s;
    uint32_t x = 0x12345678u;
    unsigned i;

    for (i = 0; i < 1024; i++) {
        /* xorshift32: balanced enough for the rule. */
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        w[i] = x;
    }
    diag_rng_stats(w, 1024, &s);
    CHECK_EQ(s.bits, 32768);
    CHECK_EQ(s.repeats, 0);
    CHECK(diag_rng_ok(&s));
    /* One repeat fails. */
    w[10] = w[9];
    diag_rng_stats(w, 1024, &s);
    CHECK_EQ(s.repeats, 1);
    CHECK(!diag_rng_ok(&s));
    /* A stuck generator. */
    memset(w, 0, sizeof(w));
    diag_rng_stats(w, 1024, &s);
    CHECK_EQ(s.repeats, 1023);
    CHECK_EQ(s.ones, 0);
    CHECK(!diag_rng_ok(&s));
    /* Biased but never repeating: alternate two words with many ones. */
    for (i = 0; i < 1024; i++) {
        w[i] = (i & 1u) ? 0xffffff0fu : 0xfffff0ffu;
    }
    diag_rng_stats(w, 1024, &s);
    CHECK_EQ(s.repeats, 0);
    CHECK_EQ(s.ones, 28u * 1024u);
    CHECK(!diag_rng_ok(&s));
}

TEST(rng_slack_edges)
{
    diag_rng_stats_t s = { 1024, 0, 16384 + DIAG_RNG_SLACK, 32768 };

    CHECK(diag_rng_ok(&s));
    s.ones++;
    CHECK(!diag_rng_ok(&s));
    s.ones = 16384 - DIAG_RNG_SLACK;
    CHECK(diag_rng_ok(&s));
    s.ones--;
    CHECK(!diag_rng_ok(&s));
}

TEST(timer_ppm)
{
    CHECK_EQ(diag_timer_ppm(1000000, 100, 10000), 0);
    CHECK_EQ(diag_timer_ppm(1000000, 101, 10000), 10000);
    CHECK_EQ(diag_timer_ppm(1000000, 99, 10000), -10000);
    CHECK_EQ(diag_timer_ppm(1002000, 100, 10000), -1996);
    CHECK_EQ(diag_timer_ppm(0, 100, 10000), 0);
    CHECK_EQ(diag_timer_ppm(1000000, 0, 10000), -1000000);
    CHECK_EQ(diag_timer_ppm(1000, 0xffffffffu, 0xffffffffu), 1000000);
}

/* The window the diagnostic uses: 200 ticks of 10000 us. */
TEST(timer_window)
{
    CHECK_EQ(DIAG_TIMER_TICKS, 200u);
    CHECK_EQ(DIAG_TIMER_PPM_MAX, 5000);
    /* Exact: both stamps equally late. */
    CHECK_EQ(diag_timer_ppm(2000000, 200, 10000), 0);
    /* The last stamp 1 us late or early: the timer's resolution. */
    CHECK_EQ(diag_timer_ppm(2000001, 200, 10000), 0);
    CHECK_EQ(diag_timer_ppm(1999998, 200, 10000), 1);
    /* One interrupt 300 us later than the other. */
    CHECK_EQ(diag_timer_ppm(2000300, 200, 10000), -149);
    CHECK_EQ(diag_timer_ppm(1999700, 200, 10000), 150);
    /* Almost a whole period late, still no missed tick: inside the limit. */
    CHECK_EQ(diag_timer_ppm(2009999, 200, 10000), -4974);
    CHECK(diag_timer_ok(-4974, 0));
    CHECK(diag_timer_ok(4999, 0));
    /* A tick 1% fast: 200 periods of 9900 us on the timer. */
    CHECK_EQ(diag_timer_ppm(1980000, 200, 10000), 10101);
    CHECK(!diag_timer_ok(10101, 0));
    /* 1% slow. */
    CHECK_EQ(diag_timer_ppm(2020000, 200, 10000), -9900);
    CHECK(!diag_timer_ok(-9900, 0));
    /* Every tenth interrupt lost: 200 callbacks over 222 periods. */
    CHECK_EQ(diag_timer_ppm(2220000, 200, 10000), -99099);
    CHECK_EQ(diag_timer_missed(222, 200), 22);
    CHECK(!diag_timer_ok(-99099, 22));
}

TEST(timer_limits)
{
    CHECK(diag_timer_ok(0, 0));
    CHECK(diag_timer_ok(DIAG_TIMER_PPM_MAX, 0));
    CHECK(diag_timer_ok(-DIAG_TIMER_PPM_MAX, 0));
    CHECK(!diag_timer_ok(DIAG_TIMER_PPM_MAX + 1, 0));
    CHECK(!diag_timer_ok(-DIAG_TIMER_PPM_MAX - 1, 0));
    /* One missed tick fails on its own: 201 periods for 200 callbacks is only -4975 ppm. */
    CHECK_EQ(diag_timer_ppm(2010000, 200, 10000), -4975);
    CHECK(!diag_timer_ok(-4975, 1));
    CHECK_EQ(diag_timer_missed(201, 200), 1);
    CHECK_EQ(diag_timer_missed(200, 200), 0);
    CHECK_EQ(diag_timer_missed(0, 200), 0);
    CHECK_EQ(diag_timer_missed(0xffffffffu, 0), 0xffffffffu);
}

TEST(kib_per_s)
{
    CHECK_EQ(diag_kib_per_s(131072, 1000000), 128);
    CHECK_EQ(diag_kib_per_s(131072, 500000), 256);
    CHECK_EQ(diag_kib_per_s(131072, 0), 0);
    CHECK_EQ(diag_kib_per_s(1ull << 40, 1), 0xffffffffu);
}
