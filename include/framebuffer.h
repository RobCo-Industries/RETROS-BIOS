#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t *buffer;
} framebuffer_t;

// Initialize framebuffer
int fb_init(uint32_t width, uint32_t height, uint32_t depth);

// Get framebuffer info
framebuffer_t *fb_get_info(void);

// Draw a pixel
void fb_draw_pixel(uint32_t x, uint32_t y, uint32_t color);

// Clear screen
void fb_clear(uint32_t color);

// Draw a character using 8x16 font
void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);

// Draw a string
void fb_draw_string(uint32_t x, uint32_t y, const char *str, uint32_t fg, uint32_t bg);

// Apply scanline effect
void fb_apply_scanlines(void);

#endif // FRAMEBUFFER_H
