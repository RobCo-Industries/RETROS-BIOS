#include "uart.h"
#include "hardware.h"
#include <stdarg.h>

void uart_init(void) {
    // Disable UART0
    MMIO_WRITE(UART0_CR, 0);

    // Setup GPIO pins 14 & 15 for UART
    uint32_t ra = MMIO_READ(GPIO_BASE + 0x04);  // GPFSEL1
    ra &= ~(7 << 12);  // Clear GPIO 14
    ra |= (4 << 12);   // Set ALT0
    ra &= ~(7 << 15);  // Clear GPIO 15
    ra |= (4 << 15);   // Set ALT0
    MMIO_WRITE(GPIO_BASE + 0x04, ra);

    // Disable pull up/down for pins 14, 15
    MMIO_WRITE(GPIO_BASE + 0x94, 0);  // GPPUD
    delay_cycles(150);
    MMIO_WRITE(GPIO_BASE + 0x98, (1 << 14) | (1 << 15));  // GPPUDCLK0
    delay_cycles(150);
    MMIO_WRITE(GPIO_BASE + 0x98, 0);  // Remove clock

    // Clear pending interrupts
    MMIO_WRITE(UART0_ICR, 0x7FF);

    // Set baud rate to 115200
    // Assuming 3MHz UART clock: divider = 3000000 / (16 * 115200) = 1.627
    MMIO_WRITE(UART0_IBRD, 1);
    MMIO_WRITE(UART0_FBRD, 40);

    // Enable FIFO & 8 bit data transmission (1 stop bit, no parity)
    MMIO_WRITE(UART0_LCRH, (1 << 4) | (1 << 5) | (1 << 6));

    // Enable UART0, receive & transfer
    MMIO_WRITE(UART0_CR, (1 << 0) | (1 << 8) | (1 << 9));
}

void uart_putc(char c) {
    // Wait for UART to become ready to transmit
    while (MMIO_READ(UART0_FR) & (1 << 5)) { }
    MMIO_WRITE(UART0_DR, c);
}

void uart_puts(const char *str) {
    while (*str) {
        if (*str == '\n') {
            uart_putc('\r');
        }
        uart_putc(*str++);
    }
}

char uart_getc(void) {
    // Wait for UART to have received something
    while (MMIO_READ(UART0_FR) & (1 << 4)) { }
    return MMIO_READ(UART0_DR) & 0xFF;
}

int uart_data_available(void) {
    return !(MMIO_READ(UART0_FR) & (1 << 4));
}

// Simple printf implementation
static void uart_print_int(int val) {
    if (val == 0) {
        uart_putc('0');
        return;
    }

    char buffer[12];
    int i = 0;
    int is_negative = 0;

    if (val < 0) {
        is_negative = 1;
        // Handle INT_MIN edge case safely
        if (val == -2147483648) {
            uart_puts("-2147483648");
            return;
        }
        val = -val;
    }

    while (val > 0) {
        buffer[i++] = '0' + (val % 10);
        val /= 10;
    }

    if (is_negative) {
        uart_putc('-');
    }

    while (i > 0) {
        uart_putc(buffer[--i]);
    }
}

static void uart_print_hex(uint32_t val) {
    uart_puts("0x");
    for (int i = 7; i >= 0; i--) {
        uint32_t nibble = (val >> (i * 4)) & 0xF;
        uart_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
}

void uart_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'd':
                    uart_print_int(va_arg(args, int));
                    break;
                case 'x':
                    uart_print_hex(va_arg(args, uint32_t));
                    break;
                case 's':
                    uart_puts(va_arg(args, char*));
                    break;
                case 'c':
                    uart_putc((char)va_arg(args, int));
                    break;
                case '%':
                    uart_putc('%');
                    break;
                default:
                    uart_putc('%');
                    uart_putc(*fmt);
                    break;
            }
        } else {
            if (*fmt == '\n') {
                uart_putc('\r');
            }
            uart_putc(*fmt);
        }
        fmt++;
    }

    va_end(args);
}
