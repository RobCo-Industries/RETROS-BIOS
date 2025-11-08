#include "hardware.h"

void delay_cycles(uint32_t count) {
    while (count--) {
        asm volatile("nop");
    }
}

void delay_us(uint32_t microseconds) {
    // Approximate delay based on system timer
    uint32_t start = MMIO_READ(TIMER_CLO);
    while ((MMIO_READ(TIMER_CLO) - start) < microseconds) {
        asm volatile("nop");
    }
}

void delay_ms(uint32_t milliseconds) {
    delay_us(milliseconds * 1000);
}
