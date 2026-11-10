#include "drivers/framebuffer.h"

#include <stddef.h>

static struct framebuffer fb;

bool fb_init(struct limine_framebuffer *limine_fb) {
    if (limine_fb->memory_model != LIMINE_FRAMEBUFFER_RGB || limine_fb->bpp != 32
        || limine_fb->red_mask_size != 8 || limine_fb->green_mask_size != 8
        || limine_fb->blue_mask_size != 8) {
        return false;
    }

    fb.virt_addr   = limine_fb->address;
    fb.width       = limine_fb->width;
    fb.height      = limine_fb->height;
    fb.pitch       = limine_fb->pitch;
    fb.red_shift   = limine_fb->red_mask_shift;
    fb.green_shift = limine_fb->green_mask_shift;
    fb.blue_shift  = limine_fb->blue_mask_shift;
    return true;
}

const struct framebuffer *fb_get(void) {
    return &fb;
}

uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << fb.red_shift)
         | ((uint32_t)g << fb.green_shift)
         | ((uint32_t)b << fb.blue_shift);
}

void fb_put_pixel(uint64_t x, uint64_t y, uint32_t color) {
    if (x >= fb.width || y >= fb.height) {
        return;   // ignore pixels off screen instead of writing past the buffer
    }
    // pitch is in bytes; divide by 4 to get pixels (uint32_t) per row.
    fb.virt_addr[y * (fb.pitch / 4) + x] = color;
}

void fb_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color) {
    for (uint64_t row = y; row < y + h; row++) {
        for (uint64_t col = x; col < x + w; col++) {
            fb_put_pixel(col, row, color);
        }
    }
}

void fb_clear(uint32_t color) {
    fb_fill_rect(0, 0, fb.width, fb.height, color);
}

void fb_scroll_region_up(uint64_t y_start, uint64_t lines, uint32_t fill_color) {
    if (y_start >= fb.height) {
        return;
    }
    uint64_t region_height = fb.height - y_start;
    if (lines >= region_height) {
        fb_fill_rect(0, y_start, fb.width, region_height, fill_color);
        return;
    }
    uint64_t pixels_per_row = fb.pitch / 4;
    // Copy each row from "lines" rows below. Top to bottom is safe because
    // we always read from below the row we write.
    for (uint64_t y = y_start; y < fb.height - lines; y++) {
        volatile uint32_t *dst = fb.virt_addr + y * pixels_per_row;
        volatile uint32_t *src = fb.virt_addr + (y + lines) * pixels_per_row;
        for (uint64_t x = 0; x < fb.width; x++) {
            dst[x] = src[x];
        }
    }
    fb_fill_rect(0, fb.height - lines, fb.width, lines, fill_color);
}

void fb_scroll_up(uint64_t lines, uint32_t fill_color) {
    fb_scroll_region_up(0, lines, fill_color);
}
