/*
 * Power-on self test: the memory test and the result table. Pure code:
 * it builds and runs on the host in the unit tests. post_hw.c runs the
 * hardware probes and fills the table.
 */
#ifndef RETROS_POST_H
#define RETROS_POST_H

#include <stddef.h>
#include <stdint.h>

/*
 * Memory test window: 256 KiB at the start of the OS slot. The BIOS owns
 * no part of that slot, nothing lives there before MFBootAgent loads an
 * OS, and it is outside the BIOS image, its stacks, the handoff area at
 * 0x00100000 and the MFBootAgent slot at 0x00200000. See MEMORY_MAP.md.
 */
#define POST_MEM_BASE   0x00400000u
#define POST_MEM_SIZE   0x00040000u

/* Result of one probe. */
#define POST_OK         0
#define POST_FAIL       1
#define POST_ABSENT     2
#define POST_SKIPPED    3   /* not run: an earlier probe stopped the self test */

/*
 * Probes, in the order they run and show. Memory comes first: every
 * later probe and step needs it. Video comes next: the framebuffer and
 * the mailbox do not use the timer, so a working screen shows OK even
 * when the timer is stopped. Then the timer: the SD card, the RTC and
 * the audio wait on it with deadlines that use the same timer, so a
 * stopped timer would hang them.
 */
enum {
    POST_MEMORY,
    POST_VIDEO,
    POST_TIMER,
    POST_SD,
    POST_FAT,
    POST_RNG,
    POST_RTC,
    POST_AUDIO,
    POST_COUNT
};

#define POST_DETAIL_MAX 48

typedef struct {
    int result;                     /* POST_OK, POST_FAIL, POST_ABSENT or POST_SKIPPED */
    char detail[POST_DETAIL_MAX];   /* short text for the screen, may be empty */
} post_item_t;

typedef struct {
    post_item_t item[POST_COUNT];
} post_table_t;

typedef struct {
    unsigned ok, fail, absent, skipped;
} post_counts_t;

/* Marker name of a probe, such as "memory". "unknown" past the end. */
const char *post_name(unsigned probe);
/* Screen label of a probe, such as "MEMORY". */
const char *post_label(unsigned probe);
/* "ok", "fail", "absent" or "skipped". "unknown" for other values. */
const char *post_result_name(int result);

/* Every probe starts as POST_SKIPPED with no detail: a probe that never runs stays so. */
void post_table_init(post_table_t *t);
void post_count(const post_table_t *t, post_counts_t *out);

/*
 * Nonzero when the result of `probe` stops the self test: no later probe
 * may run. That is a memory or timer probe that is not ok.
 */
int post_stops(const post_table_t *t, unsigned probe);

/*
 * Nonzero when `probe` runs although `stop`, the probe that stopped the
 * self test, came before it. Only the timer runs after a failed memory
 * test: the shell may open after that failure, and the shell waits on
 * the timer. `stop` is POST_COUNT while nothing stopped the test.
 */
int post_runs_after(unsigned stop, unsigned probe);

/*
 * Nonzero when nothing may wait on the timer: the timer probe did not
 * pass. A skipped timer counts as bad, since nobody checked it.
 */
int post_timer_bad(const post_table_t *t);

/*
 * The error a POST table stops the boot with: BIOS_ERR_MEMTEST for a
 * memory test that is not ok, then BIOS_ERR_TIMER for a timer that is
 * not ok, then BIOS_ERR_IO when the SD card or the FAT volume is not ok
 * (failed, absent or skipped), else 0. A video, RNG, RTC or audio probe
 * that is not ok does not stop the boot.
 */
int post_fatal(const post_table_t *t);

/* Where the memory test found a fault. */
typedef struct {
    uint32_t addr;      /* address of the failing word */
    uint32_t expected;
    uint32_t actual;
    const char *step;   /* "data", "address" or "pattern" */
} memtest_fault_t;

/*
 * Tests `words` 32-bit words at `base`: a walking-ones data bus test on
 * the first word, an address bus test at every power-of-two offset, then
 * two full passes (a pattern derived from each address, then its
 * complement), each written in full before it is read back. `words` must
 * be a power of two, at least 2. Returns 0, or -1 with *fault set (if not
 * NULL). Accesses: 2 writes and 2 reads per word, plus 128 for the data
 * bus test and about (log2(words) + 1)^2 for the address bus test.
 */
int memtest_run(volatile uint32_t *base, uint32_t words, memtest_fault_t *fault);

#if defined(ROBCO_HOST) || defined(RETROS_FAULT_MEMTEST)
/*
 * Host builds and the memtest fault test image only: when set, every
 * word the test reads goes through this hook, so a test can model a
 * stuck bit or a shorted address line. The shipped image has no hook.
 */
extern uint32_t (*memtest_read_hook)(volatile uint32_t *p, uint32_t value);
#endif

#endif
