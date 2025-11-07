#include "timer.h"
#include "hardware.h"

// System Timer registers (BCM2835/6/7)
#define TIMER_CS   (TIMER_BASE + 0x00)  // Control/Status
#define TIMER_CLO  (TIMER_BASE + 0x04)  // Counter Lower 32 bits
#define TIMER_CHI  (TIMER_BASE + 0x08)  // Counter Higher 32 bits
#define TIMER_C0   (TIMER_BASE + 0x0C)  // Compare 0
#define TIMER_C1   (TIMER_BASE + 0x10)  // Compare 1
#define TIMER_C2   (TIMER_BASE + 0x14)  // Compare 2
#define TIMER_C3   (TIMER_BASE + 0x18)  // Compare 3

static uint64_t boot_time = 0;

void timer_init(void) {
    // System timer runs at 1 MHz
    // Record boot time
    boot_time = timer_get_ticks();
}

uint64_t timer_get_ticks(void) {
    uint32_t hi, lo;
    
    // Read high and low parts, handling overflow
    do {
        hi = MMIO_READ(TIMER_CHI);
        lo = MMIO_READ(TIMER_CLO);
    } while (hi != MMIO_READ(TIMER_CHI));
    
    return ((uint64_t)hi << 32) | lo;
}

void timer_wait_us(uint32_t microseconds) {
    uint64_t start = timer_get_ticks();
    uint64_t end = start + microseconds;
    
    // Handle potential overflow
    if (end < start) {
        // Wait for overflow
        while (timer_get_ticks() >= start) {
            asm volatile("nop");
        }
    }
    
    // Wait for target time
    while (timer_get_ticks() < end) {
        asm volatile("nop");
    }
}

void timer_wait_ms(uint32_t milliseconds) {
    timer_wait_us(milliseconds * 1000);
}

void timer_set_interval(uint32_t microseconds) {
    // Set up timer compare register for channel 1
    uint32_t current = MMIO_READ(TIMER_CLO);
    MMIO_WRITE(TIMER_C1, current + microseconds);
    
    // Enable timer interrupt on channel 1 (bit 1)
    uint32_t cs = MMIO_READ(TIMER_CS);
    cs |= (1 << 1);
    MMIO_WRITE(TIMER_CS, cs);
}

uint64_t timer_get_uptime_us(void) {
    return timer_get_ticks() - boot_time;
}
