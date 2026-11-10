#include "drivers/console.h"

#include <stdint.h>
#include <stddef.h>
#include "drivers/framebuffer.h"

// PSF1 font file layout:
//   4-byte header, then glyph bitmaps, one after another.
//   Each glyph is 8 pixels wide and "charsize" rows tall.
//   One row = one byte; the highest bit (0x80) is the leftmost pixel.
//   (Optional unicode table at the end; we do not need it, because this
//   font keeps its glyphs in ASCII order.)
struct psf1_header {
    uint8_t magic[2];   // 0x36, 0x04
    uint8_t mode;       // bit 0 set: 512 glyphs instead of 256
    uint8_t charsize;   // bytes per glyph = height in pixels
} __attribute__((packed));

#define PSF1_MAGIC0     0x36
#define PSF1_MAGIC1     0x04
#define PSF1_MODE512    0x01
#define GLYPH_WIDTH     8

// Labels from kernel/font.asm: start and end of the font bytes in the kernel image.
extern const uint8_t font_psf_start[];
extern const uint8_t font_psf_end[];

static const struct psf1_header *font;
static const uint8_t *glyphs;       // first glyph bitmap
static uint32_t glyph_count;
static uint32_t glyph_height;

// Cursor position and screen size, counted in characters (not pixels).
static uint64_t cursor_col, cursor_row;
static uint64_t cols, rows;
static uint32_t fg_color, bg_color;
static bool ready = false;

bool console_init(void) {
    font = (const struct psf1_header *)font_psf_start;
    if (font->magic[0] != PSF1_MAGIC0 || font->magic[1] != PSF1_MAGIC1) {
        return false;
    }

    glyph_count  = (font->mode & PSF1_MODE512) ? 512 : 256;
    glyph_height = font->charsize;
    glyphs       = font_psf_start + sizeof(struct psf1_header);

    // Make sure the file really holds all the glyphs the header promises.
    size_t file_size = (size_t)(font_psf_end - font_psf_start);
    if (sizeof(struct psf1_header) + (size_t)glyph_count * glyph_height > file_size) {
        return false;
    }

    const struct framebuffer *fb = fb_get();
    cols = fb->width / GLYPH_WIDTH;
    rows = fb->height / glyph_height;
    cursor_col = 0;
    cursor_row = 0;
    fg_color = fb_rgb(0xDD, 0xDD, 0xDD);   // light grey text
    bg_color = fb_rgb(0x10, 0x10, 0x30);   // dark blue background

    fb_clear(bg_color);
    ready = true;
    return true;
}

bool console_is_ready(void) {
    return ready;
}

// Draw one glyph with its top-left corner at character cell (col, row).
static void draw_glyph(uint8_t c, uint64_t col, uint64_t row) {
    const uint8_t *glyph = glyphs + (uint32_t)c * glyph_height;
    uint64_t px = col * GLYPH_WIDTH;
    uint64_t py = row * glyph_height;

    for (uint32_t y = 0; y < glyph_height; y++) {
        uint8_t bits = glyph[y];
        for (uint32_t x = 0; x < GLYPH_WIDTH; x++) {
            // Test bits from left (0x80) to right (0x01).
            bool on = bits & (0x80 >> x);
            fb_put_pixel(px + x, py + y, on ? fg_color : bg_color);
        }
    }
}

static void new_line(void) {
    cursor_col = 0;
    cursor_row++;
    if (cursor_row >= rows) {
        // Screen full: move everything up by one text line.
        fb_scroll_up(glyph_height, bg_color);
        cursor_row = rows - 1;
    }
}

void console_putc(char c) {
    if (c == '\n') {
        new_line();
        return;
    }
    if (c == '\r') {
        cursor_col = 0;
        return;
    }

    draw_glyph((uint8_t)c, cursor_col, cursor_row);
    cursor_col++;
    if (cursor_col >= cols) {
        new_line();   // wrap long lines
    }
}

void console_write(const char *s) {
    while (*s) {
        console_putc(*s++);
    }
}
