#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>

// GPIO function select
typedef enum {
    GPIO_INPUT = 0,
    GPIO_OUTPUT = 1,
    GPIO_ALT0 = 4,
    GPIO_ALT1 = 5,
    GPIO_ALT2 = 6,
    GPIO_ALT3 = 7,
    GPIO_ALT4 = 3,
    GPIO_ALT5 = 2
} gpio_function_t;

// GPIO pull-up/down modes
typedef enum {
    GPIO_PULL_NONE = 0,
    GPIO_PULL_DOWN = 1,
    GPIO_PULL_UP = 2
} gpio_pull_t;

// Initialize GPIO subsystem
void gpio_init(void);

// Set GPIO pin function (input, output, or alternate function)
void gpio_set_function(uint32_t pin, gpio_function_t function);

// Set GPIO pin output value
void gpio_set(uint32_t pin, uint32_t value);

// Get GPIO pin input value
uint32_t gpio_get(uint32_t pin);

// Set pull-up/down resistor for GPIO pin
void gpio_set_pull(uint32_t pin, gpio_pull_t pull);

// Toggle GPIO pin output
void gpio_toggle(uint32_t pin);

#endif // GPIO_H
