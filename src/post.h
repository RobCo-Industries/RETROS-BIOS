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

/* Probes, in the order they run and show. */
enum {
    POST_MEMORY,
    POST_VIDEO,
    POST_SD,
    POST_FAT,
    POST_TIMER,
    POST_RNG,
    POST_RTC,
    POST_AUDIO,
    POST_COUNT
};

#define POST_DETAIL_MAX 40

typedef struct {
    int result;                     /* POST_OK, POST_FAIL or POST_ABSENT */
    char detail[POST_DETAIL_MAX];   /* short text for the screen, may be empty */
} post_item_t;

typedef struct {
    post_item_t item[POST_COUNT];
} post_table_t;

typedef struct {
    unsigned ok, fail, absent;
} post_counts_t;

/* Marker name of a probe, such as "memory". "unknown" past the end. */
const char *post_name(unsigned probe);
/* Screen label of a probe, such as "MEMORY". */
const char *post_label(unsigned probe);
/* "ok", "fail" or "absent". "unknown" for other values. */
const char *post_result_name(int result);

void post_table_init(post_table_t *t);
void post_count(const post_table_t *t, post_counts_t *out);

/*
 * The error a POST table stops the boot with: BIOS_ERR_MEMTEST for a
 * failed memory test, BIOS_ERR_IO when the SD card or the FAT volume is
 * not ok (failed or absent), else 0. A failed or absent video, timer,
 * RNG, RTC or audio probe does not stop the boot.
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

#ifdef ROBCO_HOST
/*
 * Host builds only: when set, every word the test reads goes through
 * this hook, so a unit test can model a stuck bit or a shorted address
 * line. The target build has no hook.
 */
extern uint32_t (*memtest_read_hook)(volatile uint32_t *p, uint32_t value);
#endif

#endif
