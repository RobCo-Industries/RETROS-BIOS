#include "framebuffer.h"
#include "hardware.h"
#include "font.h"

// Framebuffer address mask (removes VC/ARM address bit)
#define FRAMEBUFFER_ADDR_MASK 0x3FFFFFFF

static framebuffer_t fb_info;

// Mailbox property interface
typedef struct {
    uint32_t size;
    uint32_t request_code;
    uint32_t tags[32];
} mailbox_property_t;

static uint32_t mailbox_property[256] __attribute__((aligned(16)));

static int mailbox_call(uint8_t channel) {
    uint32_t addr = (uint32_t)mailbox_property;

    // Wait for mailbox to be available
    while (MMIO_READ(MAILBOX_STATUS) & MAILBOX_FULL) { }

    // Write the address of our message to the mailbox with channel identifier
    MMIO_WRITE(MAILBOX_WRITE, (addr & ~0xF) | (channel & 0xF));

    // Wait for the response
    while (1) {
        while (MMIO_READ(MAILBOX_STATUS) & MAILBOX_EMPTY) { }

        uint32_t response = MMIO_READ(MAILBOX_READ);

        if ((response & 0xF) == channel && (response & ~0xF) == addr) {
            return mailbox_property[1] == 0x80000000;
        }
    }
}

int fb_init(uint32_t width, uint32_t height, uint32_t depth) {
    int i = 0;

    // Set size
    mailbox_property[i++] = 35 * 4;  // Buffer size in bytes
    mailbox_property[i++] = 0;        // Request code

    // Set physical display size
    mailbox_property[i++] = 0x48003;  // Tag id
    mailbox_property[i++] = 8;        // Value buffer size
    mailbox_property[i++] = 8;        // Request/response size
    mailbox_property[i++] = width;
    mailbox_property[i++] = height;

    // Set virtual display size
    mailbox_property[i++] = 0x48004;
    mailbox_property[i++] = 8;
    mailbox_property[i++] = 8;
    mailbox_property[i++] = width;
    mailbox_property[i++] = height;

    // Set depth
    mailbox_property[i++] = 0x48005;
    mailbox_property[i++] = 4;
    mailbox_property[i++] = 4;
    mailbox_property[i++] = depth;

    // Allocate framebuffer
    mailbox_property[i++] = 0x40001;
    mailbox_property[i++] = 8;
    mailbox_property[i++] = 8;
    mailbox_property[i++] = 16;       // Alignment
    mailbox_property[i++] = 0;        // Size returned here

    // Get pitch
    mailbox_property[i++] = 0x40008;
    mailbox_property[i++] = 4;
    mailbox_property[i++] = 4;
    mailbox_property[i++] = 0;        // Pitch returned here

    // End tag
    mailbox_property[i++] = 0;

    if (!mailbox_call(8)) {
        return -1;
    }

    // Extract framebuffer info
    fb_info.width = mailbox_property[5];
    fb_info.height = mailbox_property[6];
    fb_info.pitch = mailbox_property[33];
    fb_info.buffer = (uint32_t *)(mailbox_property[28] & FRAMEBUFFER_ADDR_MASK);

    return 0;
}

framebuffer_t *fb_get_info(void) {
    return &fb_info;
}

void fb_draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb_info.width || y >= fb_info.height) {
        return;
    }
    fb_info.buffer[y * (fb_info.pitch / 4) + x] = color;
}

void fb_clear(uint32_t color) {
    for (uint32_t y = 0; y < fb_info.height; y++) {
        for (uint32_t x = 0; x < fb_info.width; x++) {
            fb_draw_pixel(x, y, color);
        }
    }
}

void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = font8x16[(uint8_t)c];

    for (int row = 0; row < 16; row++) {
        uint8_t line = glyph[row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (line & (1 << (7 - col))) ? fg : bg;
            fb_draw_pixel(x + col, y + row, color);
        }
    }
}

void fb_draw_string(uint32_t x, uint32_t y, const char *str, uint32_t fg, uint32_t bg) {
    uint32_t current_x = x;
    while (*str) {
        if (*str == '\n') {
            current_x = x;
            y += 16;
        } else {
            fb_draw_char(current_x, y, *str, fg, bg);
            current_x += 8;
        }
        str++;
    }
}

void fb_apply_scanlines(void) {
    // Apply scanline effect (darken every other line)
    for (uint32_t y = 1; y < fb_info.height; y += 2) {
        for (uint32_t x = 0; x < fb_info.width; x++) {
            uint32_t idx = y * (fb_info.pitch / 4) + x;
            uint32_t color = fb_info.buffer[idx];

            // Darken the color
            uint32_t r = ((color >> 16) & 0xFF) * 3 / 4;
            uint32_t g = ((color >> 8) & 0xFF) * 3 / 4;
            uint32_t b = (color & 0xFF) * 3 / 4;

            fb_info.buffer[idx] = (r << 16) | (g << 8) | b;
        }
    }
}
