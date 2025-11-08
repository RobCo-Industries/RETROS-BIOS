#include "gpio.h"
#include "hardware.h"

// GPIO registers
#define GPFSEL0   (GPIO_BASE + 0x00)  // Function Select 0
#define GPFSEL1   (GPIO_BASE + 0x04)  // Function Select 1
#define GPFSEL2   (GPIO_BASE + 0x08)  // Function Select 2
#define GPFSEL3   (GPIO_BASE + 0x0C)  // Function Select 3
#define GPFSEL4   (GPIO_BASE + 0x10)  // Function Select 4
#define GPFSEL5   (GPIO_BASE + 0x14)  // Function Select 5

#define GPSET0    (GPIO_BASE + 0x1C)  // Pin Output Set 0
#define GPSET1    (GPIO_BASE + 0x20)  // Pin Output Set 1

#define GPCLR0    (GPIO_BASE + 0x28)  // Pin Output Clear 0
#define GPCLR1    (GPIO_BASE + 0x2C)  // Pin Output Clear 1

#define GPLEV0    (GPIO_BASE + 0x34)  // Pin Level 0
#define GPLEV1    (GPIO_BASE + 0x38)  // Pin Level 1

#define GPPUD     (GPIO_BASE + 0x94)  // Pull-up/down Enable
#define GPPUDCLK0 (GPIO_BASE + 0x98)  // Pull-up/down Clock 0
#define GPPUDCLK1 (GPIO_BASE + 0x9C)  // Pull-up/down Clock 1

void gpio_init(void) {
    // GPIO is automatically initialized on boot
    // This function can be used for custom initialization
}

void gpio_set_function(uint32_t pin, gpio_function_t function) {
    if (pin > 53) return;  // BCM2835 has 54 GPIO pins (0-53)

    uint32_t reg_offset = (pin / 10) * 4;
    uint32_t bit_offset = (pin % 10) * 3;

    uint32_t reg_addr = GPIO_BASE + reg_offset;
    uint32_t reg_val = MMIO_READ(reg_addr);

    // Clear the three bits for this pin
    reg_val &= ~(7 << bit_offset);

    // Set the new function
    reg_val |= ((uint32_t)function << bit_offset);

    MMIO_WRITE(reg_addr, reg_val);
}

void gpio_set(uint32_t pin, uint32_t value) {
    if (pin > 53) return;

    uint32_t reg = (pin < 32) ? GPSET0 : GPSET1;
    uint32_t bit = pin % 32;

    if (value) {
        MMIO_WRITE(reg, 1 << bit);
    } else {
        reg = (pin < 32) ? GPCLR0 : GPCLR1;
        MMIO_WRITE(reg, 1 << bit);
    }
}

uint32_t gpio_get(uint32_t pin) {
    if (pin > 53) return 0;

    uint32_t reg = (pin < 32) ? GPLEV0 : GPLEV1;
    uint32_t bit = pin % 32;

    return (MMIO_READ(reg) >> bit) & 1;
}

void gpio_set_pull(uint32_t pin, gpio_pull_t pull) {
    if (pin > 53) return;

    // Set pull-up/down control
    MMIO_WRITE(GPPUD, (uint32_t)pull);

    // Wait 150 cycles (as per BCM2835 datasheet)
    for (int i = 0; i < 150; i++) {
        asm volatile("nop");
    }

    // Clock the control signal into the GPIO pin
    uint32_t clk_reg = (pin < 32) ? GPPUDCLK0 : GPPUDCLK1;
    uint32_t bit = pin % 32;
    MMIO_WRITE(clk_reg, 1 << bit);

    // Wait 150 cycles
    for (int i = 0; i < 150; i++) {
        asm volatile("nop");
    }

    // Remove the control signal
    MMIO_WRITE(GPPUD, 0);
    MMIO_WRITE(clk_reg, 0);
}

void gpio_toggle(uint32_t pin) {
    if (pin > 53) return;

    uint32_t current = gpio_get(pin);
    gpio_set(pin, !current);
}
