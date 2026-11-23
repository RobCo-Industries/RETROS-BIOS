/* POST: the memory test with modelled faults, and the result table rules. */

#include "boot.h"
#include "post.h"
#include "rc_test.h"

#define WORDS 4096u

static uint32_t mem[WORDS];

/* Fault models for memtest_read_hook. */
static uint32_t stuck_mask;
static volatile uint32_t *stuck_at;
static uint32_t alias_bit;

static uint32_t stuck_bit(volatile uint32_t *p, uint32_t v)
{
    return p == stuck_at ? v | stuck_mask : v;
}

/* Address line `alias_bit` (in words) stuck at 0: reads see the lower twin. */
static uint32_t shorted_line(volatile uint32_t *p, uint32_t v)
{
    size_t i = (size_t)(p - (volatile uint32_t *)mem);

    (void)v;
    return mem[i & ~(size_t)alias_bit];
}

TEST(memtest_passes_on_good_memory)
{
    memtest_fault_t f;

    memtest_read_hook = NULL;
    CHECK_EQ(memtest_run(mem, WORDS, &f), 0);
    /* The last pass leaves a different value in every word. */
    CHECK(mem[0] != mem[1] && mem[1] != mem[WORDS - 1]);
}

TEST(memtest_finds_stuck_data_bit)
{
    memtest_fault_t f;

    stuck_at = &mem[0];
    stuck_mask = 1u << 7;
    memtest_read_hook = stuck_bit;
    CHECK_EQ(memtest_run(mem, WORDS, &f), -1);
    memtest_read_hook = NULL;
    CHECK_STR(f.step, "data");
    CHECK_EQ(f.addr, (uint32_t)(uintptr_t)&mem[0]);
    CHECK_EQ(f.expected, 1);
    CHECK_EQ(f.actual, 1u | (1u << 7));
}

TEST(memtest_finds_stuck_bit_in_one_word)
{
    memtest_fault_t f;

    stuck_at = &mem[1234];
    stuck_mask = 1u << 31;
    memtest_read_hook = stuck_bit;
    CHECK_EQ(memtest_run(mem, WORDS, &f), -1);
    memtest_read_hook = NULL;
    CHECK_STR(f.step, "pattern");
    CHECK_EQ(f.addr, (uint32_t)(uintptr_t)&mem[1234]);
    CHECK(f.expected != f.actual);
}

TEST(memtest_finds_shorted_address_line)
{
    memtest_fault_t f;

    alias_bit = 64;
    memtest_read_hook = shorted_line;
    CHECK_EQ(memtest_run(mem, WORDS, &f), -1);
    memtest_read_hook = NULL;
    CHECK_STR(f.step, "address");
}

TEST(memtest_rejects_bad_sizes)
{
    memtest_fault_t f;

    CHECK_EQ(memtest_run(mem, 0, &f), -1);
    CHECK_EQ(memtest_run(mem, 1, &f), -1);
    CHECK_EQ(memtest_run(mem, 3000, &f), -1);
    CHECK_EQ(memtest_run(NULL, WORDS, NULL), -1);
}

TEST(window_outside_bios_handoff_and_slot)
{
    /* MEMORY_MAP.md: BIOS below 0x00100000, handoff to 0x0011FFFF, reserved to
     * 0x001FFFFF, MFBootAgent slot 0x00200000-0x003FFFFF. */
    CHECK(POST_MEM_BASE >= 0x00400000u);
    CHECK(POST_MEM_BASE + POST_MEM_SIZE <= 0x01000000u);
    CHECK_EQ(POST_MEM_SIZE & (POST_MEM_SIZE - 1), 0);
}

static void all(post_table_t *t, int result)
{
    unsigned i;

    for (i = 0; i < POST_COUNT; i++) {
        t->item[i].result = result;
    }
}

TEST(table_starts_skipped)
{
    post_table_t t;
    post_counts_t c;
    unsigned i;

    post_table_init(&t);
    post_count(&t, &c);
    CHECK_EQ(c.skipped, POST_COUNT);
    CHECK_EQ(c.ok + c.fail + c.absent, 0);
    for (i = 0; i < POST_COUNT; i++) {
        CHECK_EQ(t.item[i].result, POST_SKIPPED);
        CHECK_STR(t.item[i].detail, "");
    }
}

TEST(only_memory_and_timer_stop_the_probes)
{
    post_table_t t;
    unsigned i;

    post_table_init(&t);
    all(&t, POST_OK);
    for (i = 0; i < POST_COUNT; i++) {
        CHECK_EQ(post_stops(&t, i), 0);
    }
    all(&t, POST_FAIL);
    for (i = 0; i < POST_COUNT; i++) {
        CHECK_EQ(post_stops(&t, i), i == POST_MEMORY || i == POST_TIMER);
    }
    all(&t, POST_ABSENT);
    CHECK(post_stops(&t, POST_TIMER));
    CHECK(!post_stops(&t, POST_SD));
    CHECK_EQ(post_stops(&t, POST_COUNT), 0);
}

TEST(fatal_rules)
{
    post_table_t t;
    post_counts_t c;

    post_table_init(&t);
    all(&t, POST_OK);
    CHECK_EQ(post_fatal(&t), 0);
    t.item[POST_RTC].result = POST_ABSENT;
    t.item[POST_AUDIO].result = POST_ABSENT;
    t.item[POST_RNG].result = POST_FAIL;
    t.item[POST_VIDEO].result = POST_FAIL;
    CHECK_EQ(post_fatal(&t), 0);
    post_count(&t, &c);
    CHECK_EQ(c.ok, 4);
    CHECK_EQ(c.fail, 2);
    CHECK_EQ(c.absent, 2);
    CHECK_EQ(c.skipped, 0);

    all(&t, POST_OK);
    t.item[POST_SD].result = POST_ABSENT;
    CHECK_EQ(post_fatal(&t), BIOS_ERR_IO);
    all(&t, POST_OK);
    t.item[POST_FAT].result = POST_FAIL;
    CHECK_EQ(post_fatal(&t), BIOS_ERR_IO);
    all(&t, POST_OK);
    t.item[POST_FAT].result = POST_SKIPPED;
    CHECK_EQ(post_fatal(&t), BIOS_ERR_IO);
    /* A stopped timer wins over a missing card. */
    t.item[POST_TIMER].result = POST_FAIL;
    CHECK_EQ(post_fatal(&t), BIOS_ERR_TIMER);
    /* A failed memory test wins over both. */
    t.item[POST_MEMORY].result = POST_FAIL;
    CHECK_EQ(post_fatal(&t), BIOS_ERR_MEMTEST);
}

/* The table the BIOS shows after a stopped timer: the later probes skipped. */
TEST(timer_stop_table)
{
    post_table_t t;
    post_counts_t c;

    post_table_init(&t);
    t.item[POST_MEMORY].result = POST_OK;
    t.item[POST_VIDEO].result = POST_OK;
    t.item[POST_TIMER].result = POST_FAIL;
    post_count(&t, &c);
    CHECK_EQ(c.ok, 2);
    CHECK_EQ(c.fail, 1);
    CHECK_EQ(c.absent, 0);
    CHECK_EQ(c.skipped, POST_COUNT - 3);
    CHECK_EQ(post_fatal(&t), BIOS_ERR_TIMER);
}

/* After a failed memory test only the timer runs: the shell may open and waits on it. */
TEST(timer_runs_after_memory_failure)
{
    unsigned stop, i;

    for (stop = 0; stop <= POST_COUNT; stop++) {
        for (i = 0; i < POST_COUNT; i++) {
            CHECK_EQ(post_runs_after(stop, i), stop == POST_MEMORY && i == POST_TIMER);
        }
    }
}

/*
 * The rule the boot and the shell's POST share: the shell opens after a
 * failed memory test only when the timer passed. Every memory and timer
 * result together.
 */
TEST(shell_needs_a_tested_timer)
{
    static const int mem[] = { POST_OK, POST_FAIL };
    static const int tim[] = { POST_OK, POST_FAIL, POST_SKIPPED, POST_ABSENT };
    post_table_t t;
    unsigned m, k;

    for (m = 0; m < 2; m++) {
        for (k = 0; k < 4; k++) {
            post_table_init(&t);
            all(&t, POST_OK);
            t.item[POST_MEMORY].result = mem[m];
            t.item[POST_TIMER].result = tim[k];
            CHECK_EQ(post_timer_bad(&t), tim[k] != POST_OK);
            CHECK_EQ(post_fatal(&t), mem[m] != POST_OK ? BIOS_ERR_MEMTEST
                                     : tim[k] != POST_OK ? BIOS_ERR_TIMER : 0);
        }
    }
}

/* Memory, then video, then the timer: video runs even when the timer is stopped. */
TEST(probe_order)
{
    CHECK_EQ(POST_MEMORY, 0);
    CHECK_EQ(POST_VIDEO, 1);
    CHECK_EQ(POST_TIMER, 2);
    CHECK_EQ(POST_SD, 3);
    CHECK_STR(post_name(1), "video");
    CHECK_STR(post_name(2), "timer");
    CHECK_STR(post_label(1), "VIDEO");
}

TEST(names)
{
    CHECK_STR(post_name(POST_MEMORY), "memory");
    CHECK_STR(post_name(POST_AUDIO), "audio");
    CHECK_STR(post_name(POST_COUNT), "unknown");
    CHECK_STR(post_label(POST_SD), "SD CARD");
    CHECK_STR(post_result_name(POST_OK), "ok");
    CHECK_STR(post_result_name(POST_FAIL), "fail");
    CHECK_STR(post_result_name(POST_ABSENT), "absent");
    CHECK_STR(post_result_name(POST_SKIPPED), "skipped");
    CHECK_STR(post_name(POST_TIMER), "timer");
    CHECK_STR(post_label(POST_TIMER), "TIMER");
    CHECK_STR(post_result_name(9), "unknown");
}
