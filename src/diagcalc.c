#include "diagcalc.h"

#include "post.h"

/* The memtest fault hook of post.h covers these reads too. */
static uint32_t rd(volatile uint32_t *p)
{
    uint32_t v = *p;

#if defined(ROBCO_HOST) || defined(RETROS_FAULT_MEMTEST) || \
    defined(RETROS_FAULT_MEMTEST_LATE)
    if (memtest_read_hook != 0) {
        v = memtest_read_hook(p, v);
    }
#endif
    return v;
}

static uint32_t addr_of(volatile uint32_t *p)
{
    return (uint32_t)(uintptr_t)p;
}

int diag_mem_window(uint32_t mem_base, uint32_t mem_size, uint32_t fb_addr, uint32_t fb_size,
                    uint32_t *base, uint32_t *size)
{
    uint64_t lo = DIAG_MEM_LOW > mem_base ? DIAG_MEM_LOW : mem_base;
    uint64_t hi = (uint64_t)mem_base + mem_size;
    uint64_t fb_lo = fb_addr;
    uint64_t fb_hi = (uint64_t)fb_addr + fb_size;

    lo = (lo + DIAG_MIB - 1) & ~(uint64_t)(DIAG_MIB - 1);
    if (hi > 0x100000000ull) {
        hi = 0x100000000ull;
    }
    if (fb_size != 0 && fb_lo < hi && fb_hi > lo) {
        /* Whole MiB on either side of the framebuffer. */
        uint64_t below_hi = fb_lo & ~(uint64_t)(DIAG_MIB - 1);
        uint64_t above_lo = (fb_hi + DIAG_MIB - 1) & ~(uint64_t)(DIAG_MIB - 1);
        uint64_t below = below_hi > lo ? below_hi - lo : 0;
        uint64_t above = hi > above_lo ? ((hi - above_lo) & ~(uint64_t)(DIAG_MIB - 1)) : 0;

        if (below >= above) {
            hi = lo + below;
        } else {
            lo = above_lo;
            hi = above_lo + above;
        }
    }
    hi &= ~(uint64_t)(DIAG_MIB - 1);
    if (hi <= lo || hi - lo >= 0x100000000ull) {
        return -1;
    }
    *base = (uint32_t)lo;
    *size = (uint32_t)(hi - lo);
    return 0;
}

int diag_mem_allowed(uint32_t addr, uint32_t len, uint32_t mem_base, uint32_t mem_size,
                     uint32_t fb_addr, uint32_t fb_size)
{
    uint64_t end = (uint64_t)addr + len;

    if (len == 0 || addr < DIAG_MEM_LOW || addr < mem_base ||
        end > (uint64_t)mem_base + mem_size) {
        return 0;
    }
    if (fb_size != 0 && addr < (uint64_t)fb_addr + fb_size && fb_addr < end) {
        return 0;
    }
    return 1;
}

const char *diag_pat_name(int pat)
{
    switch (pat) {
    case DIAG_PAT_WALK:   return "walk";
    case DIAG_PAT_ADDR:   return "addr";
    case DIAG_PAT_CHECK:  return "check";
    case DIAG_PAT_ICHECK: return "icheck";
    default:              return "unknown";
    }
}

uint32_t diag_pat_value(int pat, uint32_t addr)
{
    uint32_t c = (addr & 4u) != 0 ? 0xaaaaaaaau : 0x55555555u;

    switch (pat) {
    case DIAG_PAT_ADDR:   return addr;
    case DIAG_PAT_CHECK:  return c;
    case DIAG_PAT_ICHECK: return ~c;
    default:              return 0;
    }
}

void diag_mem_fill(volatile uint32_t *p, uint32_t words, int pat)
{
    uint32_t i;

    for (i = 0; i < words; i++) {
        p[i] = diag_pat_value(pat, addr_of(&p[i]));
    }
}

static void set_fault(diag_mem_fault_t *f, volatile uint32_t *p, uint32_t want, uint32_t got,
                      int pat)
{
    if (f != 0) {
        f->addr = addr_of(p);
        f->expected = want;
        f->actual = got;
        f->pat = pat;
    }
}

int diag_mem_check(volatile uint32_t *p, uint32_t words, int pat, diag_mem_fault_t *f)
{
    uint32_t i;

    for (i = 0; i < words; i++) {
        uint32_t want = diag_pat_value(pat, addr_of(&p[i]));
        uint32_t got = rd(&p[i]);

        if (got != want) {
            set_fault(f, &p[i], want, got, pat);
            return -1;
        }
    }
    return 0;
}

int diag_mem_walk(volatile uint32_t *p, uint32_t words, diag_mem_fault_t *f)
{
    uint32_t i;
    unsigned b;

    for (i = 0; i < words; i += 1024u) {
        for (b = 0; b < 32; b++) {
            uint32_t want = 1u << b;
            uint32_t got;

            p[i] = want;
            got = rd(&p[i]);
            if (got != want) {
                set_fault(f, &p[i], want, got, DIAG_PAT_WALK);
                return -1;
            }
        }
    }
    return 0;
}

static unsigned popcount(uint32_t v)
{
    unsigned n = 0;

    while (v != 0) {
        v &= v - 1u;
        n++;
    }
    return n;
}

void diag_rng_stats(const uint32_t *w, uint32_t n, diag_rng_stats_t *s)
{
    uint32_t i;

    s->words = n;
    s->repeats = 0;
    s->ones = 0;
    s->bits = n * 32u;
    for (i = 0; i < n; i++) {
        s->ones += popcount(w[i]);
        if (i > 0 && w[i] == w[i - 1]) {
            s->repeats++;
        }
    }
}

int diag_rng_ok(const diag_rng_stats_t *s)
{
    uint32_t half = s->bits / 2u;
    uint32_t diff = s->ones > half ? s->ones - half : half - s->ones;

    return s->words >= 2 && s->repeats == 0 && diff <= DIAG_RNG_SLACK;
}

int32_t diag_timer_ppm(uint64_t timer_us, uint32_t ticks, uint32_t period_us)
{
    /* Both products stay below 2^64; the clamps below keep diff small. */
    uint64_t tick_us = (uint64_t)ticks * period_us;
    int64_t diff;
    int64_t ppm;

    if (timer_us == 0 || timer_us > 0x7fffffffffffull) {
        return 0;
    }
    if (tick_us >= 2 * timer_us) {
        return 1000000;
    }
    diff = (int64_t)tick_us - (int64_t)timer_us;
    if (diff > (int64_t)timer_us) {
        return 1000000;
    }
    if (diff < -(int64_t)timer_us) {
        return -1000000;
    }
    ppm = diff * 1000000 / (int64_t)timer_us;
    return (int32_t)ppm;
}

uint32_t diag_timer_missed(uint32_t periods, uint32_t ticks)
{
    return periods > ticks ? periods - ticks : 0;
}

int diag_timer_ok(int32_t ppm, uint32_t missed)
{
    return missed == 0 && ppm >= -DIAG_TIMER_PPM_MAX && ppm <= DIAG_TIMER_PPM_MAX;
}

uint32_t diag_kib_per_s(uint64_t bytes, uint64_t us)
{
    uint64_t v;

    if (us == 0) {
        return 0;
    }
    /* bytes stays below 2^40 here, so bytes * 1000000 / 1024 fits. */
    v = bytes * 1000000u / 1024u / us;
    return v > 0xffffffffu ? 0xffffffffu : (uint32_t)v;
}

int diag_audio_result(int scale_rc, int pcm_rc)
{
    if (scale_rc != 0) {
        return pcm_rc != 0 ? DIAG_AUDIO_FAIL_BOTH : DIAG_AUDIO_FAIL_SCALE;
    }
    return pcm_rc != 0 ? DIAG_AUDIO_FAIL_PCM : DIAG_AUDIO_OK;
}

const char *diag_audio_step(int result)
{
    switch (result) {
    case DIAG_AUDIO_OK: return "none";
    case DIAG_AUDIO_FAIL_SCALE: return "scale";
    case DIAG_AUDIO_FAIL_PCM: return "pcm";
    default: return "both";
    }
}
