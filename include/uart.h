#ifndef UART_H
#define UART_H

#include <stdint.h>

// Initialize UART for debugging
void uart_init(void);

// Send a single character
void uart_putc(char c);

// Send a string
void uart_puts(const char *str);

// Receive a character (blocking)
char uart_getc(void);

// Check if data is available
int uart_data_available(void);

// Printf-like function for debugging
void uart_printf(const char *fmt, ...);

#endif // UART_H
