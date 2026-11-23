#include "post.h"

#include "boot.h"

#include <robco/klibc.h>

static const char *const names[POST_COUNT] = {
    "memory", "video", "sd", "fat", "timer", "rng", "rtc", "audio",
};

static const char *const labels[POST_COUNT] = {
    "MEMORY", "VIDEO", "SD CARD", "FAT", "TIMER", "RNG", "RTC", "AUDIO",
};

const char *post_name(unsigned probe)
{
    return probe < POST_COUNT ? names[probe] : "unknown";
}

const char *post_label(unsigned probe)
{
    return probe < POST_COUNT ? labels[probe] : "UNKNOWN";
}

const char *post_result_name(int result)
{
    switch (result) {
    case POST_OK:     return "ok";
    case POST_FAIL:   return "fail";
    case POST_ABSENT: return "absent";
    default:          return "unknown";
    }
}

void post_table_init(post_table_t *t)
{
    unsigned i;

    for (i = 0; i < POST_COUNT; i++) {
        t->item[i].result = POST_ABSENT;
        t->item[i].detail[0] = '\0';
    }
}

void post_count(const post_table_t *t, post_counts_t *out)
{
    unsigned i;

    out->ok = out->fail = out->absent = 0;
    for (i = 0; i < POST_COUNT; i++) {
        switch (t->item[i].result) {
        case POST_OK:   out->ok++; break;
        case POST_FAIL: out->fail++; break;
        default:        out->absent++; break;
        }
    }
}

int post_fatal(const post_table_t *t)
{
    if (t->item[POST_MEMORY].result != POST_OK) {
        return BIOS_ERR_MEMTEST;
    }
    if (t->item[POST_SD].result != POST_OK || t->item[POST_FAT].result != POST_OK) {
        return BIOS_ERR_IO;
    }
    return 0;
}

#ifdef ROBCO_HOST
uint32_t (*memtest_read_hook)(volatile uint32_t *p, uint32_t value);

static uint32_t rd(volatile uint32_t *p)
{
    uint32_t v = *p;

    return memtest_read_hook != NULL ? memtest_read_hook(p, v) : v;
}
#else
static inline uint32_t rd(volatile uint32_t *p)
{
    return *p;
}
#endif

static int fail(memtest_fault_t *fault, volatile uint32_t *p, uint32_t expected,
                uint32_t actual, const char *step)
{
    if (fault != NULL) {
        fault->addr = (uint32_t)(uintptr_t)p;
        fault->expected = expected;
        fault->actual = actual;
        fault->step = step;
    }
    return -1;
}

/* A pattern that differs for every word and every address bit. */
static uint32_t pattern(volatile uint32_t *p)
{
    uint32_t a = (uint32_t)(uintptr_t)p;

    return (a * 0x9E3779B1u) ^ 0xA5C3F00Fu;
}

int memtest_run(volatile uint32_t *base, uint32_t words, memtest_fault_t *fault)
{
    uint32_t bit, off, i, v;

    if (base == NULL || words < 2 || (words & (words - 1)) != 0) {
        return fail(fault, base, 0, 0, "args");
    }

    /* Data bus: each bit alone, then its complement, on the first word. */
    for (bit = 1; bit != 0; bit <<= 1) {
        base[0] = bit;
        if ((v = rd(&base[0])) != bit) {
            return fail(fault, &base[0], bit, v, "data");
        }
        base[0] = ~bit;
        if ((v = rd(&base[0])) != ~bit) {
            return fail(fault, &base[0], ~bit, v, "data");
        }
    }

    /*
     * Address bus: 0xAAAAAAAA at offset 0 and at every power-of-two
     * offset. Then each of these words in turn gets 0x55555555 while the
     * others must keep their value. A stuck or shorted address line
     * makes two offsets share one word.
     */
    base[0] = 0xAAAAAAAAu;
    for (off = 1; off < words; off <<= 1) {
        base[off] = 0xAAAAAAAAu;
    }
    for (bit = 0; bit < words; bit = bit != 0 ? bit << 1 : 1) {
        base[bit] = 0x55555555u;
        for (off = 0; off < words; off = off != 0 ? off << 1 : 1) {
            if (off != bit && (v = rd(&base[off])) != 0xAAAAAAAAu) {
                return fail(fault, &base[off], 0xAAAAAAAAu, v, "address");
            }
        }
        base[bit] = 0xAAAAAAAAu;
    }

    /* Two full passes: a per-address pattern, then its complement. */
    for (i = 0; i < words; i++) {
        base[i] = pattern(&base[i]);
    }
    for (i = 0; i < words; i++) {
        if ((v = rd(&base[i])) != pattern(&base[i])) {
            return fail(fault, &base[i], pattern(&base[i]), v, "pattern");
        }
    }
    for (i = 0; i < words; i++) {
        base[i] = ~pattern(&base[i]);
    }
    for (i = 0; i < words; i++) {
        if ((v = rd(&base[i])) != ~pattern(&base[i])) {
            return fail(fault, &base[i], ~pattern(&base[i]), v, "pattern");
        }
    }
    return 0;
}
