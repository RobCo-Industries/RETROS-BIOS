#ifndef HARDWARE_H
#define HARDWARE_H

#include <stdint.h>

// Detect Raspberry Pi model at compile time
// Default to BCM2835 (RPi0/1) if not specified
#if defined(BCM2836)
    // BCM2836 for RPi2
    #define PERIPHERAL_BASE 0x3F000000
#elif defined(BCM2837)
    // BCM2837 for RPi3
    #define PERIPHERAL_BASE 0x3F000000
#else
    // BCM2835 for RPi0/1
    #define PERIPHERAL_BASE 0x20000000
#endif

// GPIO
#define GPIO_BASE (PERIPHERAL_BASE + 0x200000)

// UART0
#define UART0_BASE (PERIPHERAL_BASE + 0x201000)
#define UART0_DR     (UART0_BASE + 0x00)
#define UART0_FR     (UART0_BASE + 0x18)
#define UART0_IBRD   (UART0_BASE + 0x24)
#define UART0_FBRD   (UART0_BASE + 0x28)
#define UART0_LCRH   (UART0_BASE + 0x2C)
#define UART0_CR     (UART0_BASE + 0x30)
#define UART0_ICR    (UART0_BASE + 0x44)

// Mailbox
#define MAILBOX_BASE (PERIPHERAL_BASE + 0xB880)
#define MAILBOX_READ  (MAILBOX_BASE + 0x00)
#define MAILBOX_STATUS (MAILBOX_BASE + 0x18)
#define MAILBOX_WRITE (MAILBOX_BASE + 0x20)

#define MAILBOX_FULL  0x80000000
#define MAILBOX_EMPTY 0x40000000

// PWM
#define PWM_BASE (PERIPHERAL_BASE + 0x20C000)
#define PWM_CTL  (PWM_BASE + 0x00)
#define PWM_RNG1 (PWM_BASE + 0x10)
#define PWM_DAT1 (PWM_BASE + 0x14)

// Clock Manager
#define CM_BASE (PERIPHERAL_BASE + 0x101000)
#define CM_PWMCTL (CM_BASE + 0xA0)
#define CM_PWMDIV (CM_BASE + 0xA4)

// System Timer
#define TIMER_BASE (PERIPHERAL_BASE + 0x3000)
#define TIMER_CLO  (TIMER_BASE + 0x04)

// EMMC (SD Card)
#define EMMC_BASE (PERIPHERAL_BASE + 0x300000)

// Helper macros
#define MMIO_READ(reg) (*(volatile uint32_t *)(reg))
#define MMIO_WRITE(reg, val) (*(volatile uint32_t *)(reg) = (val))

// Delay functions
void delay_cycles(uint32_t count);
void delay_us(uint32_t microseconds);
void delay_ms(uint32_t milliseconds);

#endif // HARDWARE_H
