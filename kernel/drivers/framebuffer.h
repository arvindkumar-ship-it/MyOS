#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <limine.h>

// A linear framebuffer: a block of memory where each 32-bit value is one pixel
// on screen. Writing to it changes the screen directly.

struct framebuffer {
    volatile uint32_t *virt_addr;  // VIRTUAL address of pixel (0, 0), mapped by Limine
    uint64_t width;                // visible pixels per row
    uint64_t height;               // number of rows
    uint64_t pitch;                // BYTES per row (can be more than width * 4)
    uint8_t red_shift;             // bit position of each colour channel in a pixel
    uint8_t green_shift;
    uint8_t blue_shift;
};

// Copy what we need from Limine's framebuffer. Returns false if the
// pixel format is one we do not handle (we need 32-bit RGB, 8 bits per channel).
bool fb_init(struct limine_framebuffer *limine_fb);

// The framebuffer set up by fb_init (read-only for other drivers).
const struct framebuffer *fb_get(void);

// Build a pixel value from 8-bit red, green, blue.
uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b);

void fb_put_pixel(uint64_t x, uint64_t y, uint32_t color);
void fb_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color);
void fb_clear(uint32_t color);
