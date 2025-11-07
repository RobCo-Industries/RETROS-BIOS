#include "hardware.h"
#include "uart.h"
#include "framebuffer.h"
#include "pwm_audio.h"
#include "sdcard.h"
#include <stdint.h>
#include <stddef.h>

// Colors (RGBA format)
#define COLOR_BLACK     0x00000000
#define COLOR_GREEN     0x0000FF00
#define COLOR_DKGREEN   0x00008000
#define COLOR_AMBER     0x00FFA500
#define COLOR_RED       0x00FF0000

// Random number generator (simple LCG)
static uint32_t rng_state = 12345;

uint32_t random(void) {
    rng_state = rng_state * 1103515245 + 12345;
    return (rng_state / 65536) % 32768;
}

// Display boot messages with typing effect
void boot_message_animated(uint32_t x, uint32_t y, const char *msg, uint32_t color) {
    uint32_t current_x = x;
    while (*msg) {
        if (*msg == '\n') {
            current_x = x;
            y += 16;
        } else {
            fb_draw_char(current_x, y, *msg, color, COLOR_BLACK);
            current_x += 8;
            delay_ms(10);  // Typing effect
        }
        uart_putc(*msg);  // Also send to UART
        msg++;
    }
}

// Display memory test pattern
void memory_test_pattern(void) {
    uint32_t y_start = 200;
    
    fb_draw_string(16, y_start, "MEMORY TEST:", COLOR_GREEN, COLOR_BLACK);
    y_start += 32;
    
    for (int i = 0; i < 8; i++) {
        char buffer[64];
        uint32_t addr = 0x00100000 + (i * 0x00100000);
        uint32_t pattern = random() & 0xFFFFFF;
        
        // Create string manually (no sprintf)
        buffer[0] = '0'; buffer[1] = 'x';
        for (int j = 7; j >= 0; j--) {
            uint32_t nibble = (addr >> (j * 4)) & 0xF;
            buffer[9 - j] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
        }
        buffer[10] = ':'; buffer[11] = ' ';
        for (int j = 5; j >= 0; j--) {
            uint32_t nibble = (pattern >> (j * 4)) & 0xF;
            buffer[17 - j] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
        }
        buffer[18] = ' '; buffer[19] = 'O'; buffer[20] = 'K';
        buffer[21] = '\0';
        
        fb_draw_string(32, y_start + (i * 18), buffer, COLOR_DKGREEN, COLOR_BLACK);
        delay_ms(100);
    }
}

// Simulate bad sector warning
void bad_sector_warning(void) {
    if ((random() % 100) < 15) {  // 15% chance
        uint32_t sector = random() % 10000;
        char msg[64];
        int pos = 0;
        
        // Initialize message manually
        const char *prefix = "WARNING: Bad sector detected: ";
        while (*prefix) {
            msg[pos++] = *prefix++;
        }
        
        // Convert sector number to string
        if (sector == 0) {
            msg[pos++] = '0';
        } else {
            char temp[12];
            int i = 0;
            while (sector > 0) {
                temp[i++] = '0' + (sector % 10);
                sector /= 10;
            }
            while (i > 0) {
                msg[pos++] = temp[--i];
            }
        }
        msg[pos] = '\0';
        
        fb_draw_string(16, 400, msg, COLOR_AMBER, COLOR_BLACK);
        uart_puts(msg);
        uart_puts("\n");
        delay_ms(800);
    }
}

// Check for diagnostic mode key combination
int check_diagnostic_mode(void) {
    // Check if UART has received special sequence (e.g., "DIAG")
    // This is a simplified version
    if (uart_data_available()) {
        char c = uart_getc();
        if (c == 'D' || c == 'd') {
            return 1;
        }
    }
    return 0;
}

// Diagnostic mode display
void diagnostic_mode(void) {
    fb_clear(COLOR_BLACK);
    fb_draw_string(16, 16, "=== DIAGNOSTIC MODE ===", COLOR_AMBER, COLOR_BLACK);
    fb_draw_string(16, 48, "Hardware Status:", COLOR_GREEN, COLOR_BLACK);
    
    char *status_lines[] = {
        "UART0: OK",
        "Framebuffer: OK",
        "Timer: OK",
        "PWM: OK",
        "GPIO: OK",
        "",
        "Press any key to exit...",
        NULL
    };
    
    uint32_t y = 80;
    for (int i = 0; status_lines[i] != NULL; i++) {
        fb_draw_string(32, y, status_lines[i], COLOR_DKGREEN, COLOR_BLACK);
        y += 20;
    }
    
    fb_apply_scanlines();
    
    // Wait for key
    uart_getc();
}

// Chain-load next stage from SD card
void chain_load_next_stage(void) {
    fb_draw_string(16, 430, "Loading next stage...", COLOR_GREEN, COLOR_BLACK);
    uart_puts("Chain-loading next stage from SD card...\n");
    
    // Initialize SD card
    if (sd_init() != 0) {
        fb_draw_string(16, 450, "ERROR: SD card init failed", COLOR_RED, COLOR_BLACK);
        uart_puts("ERROR: Failed to initialize SD card\n");
        return;
    }
    
    // Read boot sector (block 0)
    uint8_t buffer[512];
    if (sd_read_block(0, buffer) != 0) {
        fb_draw_string(16, 450, "ERROR: Cannot read boot sector", COLOR_RED, COLOR_BLACK);
        uart_puts("ERROR: Failed to read boot sector\n");
        return;
    }
    
    // In a real bootloader, we would:
    // 1. Verify boot signature
    // 2. Load kernel image to memory
    // 3. Jump to kernel entry point
    
    fb_draw_string(16, 450, "Next stage loaded. Jumping...", COLOR_GREEN, COLOR_BLACK);
    uart_puts("Would jump to next stage here...\n");
    
    // For demo purposes, we'll just show a message
    delay_ms(2000);
}

// Main kernel entry point
void kernel_main(uint32_t r0 __attribute__((unused)), 
                 uint32_t r1 __attribute__((unused)), 
                 uint32_t atags __attribute__((unused))) {
    // Initialize hardware
    uart_init();
    uart_puts("\n\n");
    uart_puts("======================================\n");
    uart_puts("  RETROS-BIOS v1.0\n");
    uart_puts("  RobCo Industries (TM) Terminal\n");
    uart_puts("======================================\n\n");
    
    // Initialize framebuffer (640x480, 32-bit color)
    if (fb_init(640, 480, 32) != 0) {
        uart_puts("ERROR: Failed to initialize framebuffer\n");
        while (1) { }
    }
    
    framebuffer_t *fb = fb_get_info();
    uart_printf("Framebuffer initialized: %dx%d, pitch=%d\n", 
                fb->width, fb->height, fb->pitch);
    
    // Clear screen to black
    fb_clear(COLOR_BLACK);
    
    // Initialize PWM for audio
    pwm_audio_init();
    
    // Boot beep
    uart_puts("Playing boot beep...\n");
    pwm_boot_beep();
    
    // Display boot header with Fallout style
    uint32_t y = 16;
    fb_draw_string(16, y, "RETROS BIOS Version 1.0.0", COLOR_GREEN, COLOR_BLACK);
    y += 20;
    fb_draw_string(16, y, "Copyright (C) RobCo Industries", COLOR_DKGREEN, COLOR_BLACK);
    y += 20;
    fb_draw_string(16, y, "All Rights Reserved", COLOR_DKGREEN, COLOR_BLACK);
    y += 32;
    
    delay_ms(500);
    
    // Boot messages with animation
    boot_message_animated(16, y, "Initializing hardware...", COLOR_GREEN);
    y += 32;
    
    boot_message_animated(16, y, "Checking system memory...", COLOR_GREEN);
    y += 20;
    
    // Memory test pattern
    delay_ms(300);
    memory_test_pattern();
    
    // Bad sector warning (random)
    delay_ms(300);
    bad_sector_warning();
    
    y = 430;
    boot_message_animated(16, y, "System initialization complete.", COLOR_GREEN);
    
    delay_ms(500);
    
    // Apply scanline effect
    fb_apply_scanlines();
    
    uart_puts("\nSystem ready.\n");
    uart_puts("Press 'D' for diagnostic mode.\n");
    
    // Check for diagnostic mode
    delay_ms(1000);
    if (check_diagnostic_mode()) {
        diagnostic_mode();
        fb_clear(COLOR_BLACK);
        fb_draw_string(16, 16, "Exiting diagnostic mode...", COLOR_GREEN, COLOR_BLACK);
        delay_ms(1000);
    }
    
    // Chain-load next stage
    chain_load_next_stage();
    
    // Display final message
    fb_clear(COLOR_BLACK);
    fb_draw_string(16, 16, "RETROS BIOS HALTED", COLOR_AMBER, COLOR_BLACK);
    fb_draw_string(16, 48, "System is ready for next stage.", COLOR_GREEN, COLOR_BLACK);
    fb_apply_scanlines();
    
    uart_puts("\n\nBIOS execution complete. System halted.\n");
    
    // Halt
    while (1) {
        asm volatile("wfi");  // Wait for interrupt
    }
}
