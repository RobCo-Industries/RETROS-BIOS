#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

// System timer initialization
void timer_init(void);

// Get current timer value (microseconds)
uint64_t timer_get_ticks(void);

// Wait for a specific number of microseconds
void timer_wait_us(uint32_t microseconds);

// Wait for a specific number of milliseconds
void timer_wait_ms(uint32_t milliseconds);

// Set up a timer interrupt (if interrupts are enabled)
void timer_set_interval(uint32_t microseconds);

// Get elapsed time since boot in microseconds
uint64_t timer_get_uptime_us(void);

#endif // TIMER_H
