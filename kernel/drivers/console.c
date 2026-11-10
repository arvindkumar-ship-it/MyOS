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

// Text starts below the status bar.
#define STATUS_ROW      0
#define FIRST_TEXT_ROW  1

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
static uint32_t fg_color, bg_color, status_fg, status_bg, cursor_color;
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
    fg_color     = fb_rgb(0xDD, 0xDD, 0xDD);   // light grey text
    bg_color     = fb_rgb(0x10, 0x10, 0x30);   // dark blue background
    status_fg    = fb_rgb(0x10, 0x10, 0x30);
    status_bg    = fb_rgb(0x88, 0xAA, 0xDD);   // light blue bar
    cursor_color = fb_rgb(0xFF, 0xCC, 0x44);

    fb_clear(bg_color);
    fb_fill_rect(0, 0, fb->width, glyph_height, status_bg);
    cursor_col = 0;
    cursor_row = FIRST_TEXT_ROW;
    ready = true;
    console_set_status("MyOS");
    return true;
}

bool console_is_ready(void) {
    return ready;
}

// Draw one glyph with its top-left corner at character cell (col, row).
static void draw_glyph(uint8_t c, uint64_t col, uint64_t row, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = glyphs + (uint32_t)c * glyph_height;
    uint64_t px = col * GLYPH_WIDTH;
    uint64_t py = row * glyph_height;

    for (uint32_t y = 0; y < glyph_height; y++) {
        uint8_t bits = glyph[y];
        for (uint32_t x = 0; x < GLYPH_WIDTH; x++) {
            // Test bits from left (0x80) to right (0x01).
            bool on = bits & (0x80 >> x);
            fb_put_pixel(px + x, py + y, on ? fg : bg);
        }
    }
}

// The cursor is a 2-pixel bar at the bottom of the current cell.
static void draw_cursor(bool visible) {
    fb_fill_rect(cursor_col * GLYPH_WIDTH, cursor_row * glyph_height + glyph_height - 2,
                 GLYPH_WIDTH, 2, visible ? cursor_color : bg_color);
}

static void new_line(void) {
    cursor_col = 0;
    cursor_row++;
    if (cursor_row >= rows) {
        // Text area full: move it up by one line (the status bar stays).
        fb_scroll_region_up(FIRST_TEXT_ROW * glyph_height, glyph_height, bg_color);
        cursor_row = rows - 1;
    }
}

static void putc_raw(char c) {
    switch (c) {
    case '\n':
        new_line();
        return;
    case '\r':
        cursor_col = 0;
        return;
    case '\b':
        // Move left (also to the end of the previous line) and erase that cell.
        if (cursor_col > 0) {
            cursor_col--;
        } else if (cursor_row > FIRST_TEXT_ROW) {
            cursor_row--;
            cursor_col = cols - 1;
        }
        draw_glyph(' ', cursor_col, cursor_row, fg_color, bg_color);
        return;
    case '\f':
        console_clear();
        return;
    case '\t':
        do {
            putc_raw(' ');
        } while (cursor_col % 8 != 0);
        return;
    }

    draw_glyph((uint8_t)c, cursor_col, cursor_row, fg_color, bg_color);
    cursor_col++;
    if (cursor_col >= cols) {
        new_line();   // wrap long lines
    }
}

// ANSI escape sequences ("ESC [ <numbers> <letter>") are what terminals
// understand. Programs send them for the serial terminal; on the screen we
// support "ESC[2J" (clear) and "ESC[H" (cursor home) and skip all others,
// instead of drawing them as characters.
static enum { ESC_NONE, ESC_START, ESC_CSI } esc_state;
static int esc_param;

// Returns true if c was part of an escape sequence (and is used up).
static bool handle_escape(char c) {
    switch (esc_state) {
    case ESC_NONE:
        if (c == 27) {
            esc_state = ESC_START;
            return true;
        }
        return false;
    case ESC_START:
        esc_state = c == '[' ? ESC_CSI : ESC_NONE;
        esc_param = 0;
        return true;
    case ESC_CSI:
        if (c >= '0' && c <= '9') {
            esc_param = esc_param * 10 + (c - '0');
            return true;
        }
        if (c == ';') {
            esc_param = 0;
            return true;
        }
        // Any letter ends the sequence.
        esc_state = ESC_NONE;
        if (c == 'J' && esc_param == 2) {
            console_clear();
        } else if (c == 'H') {
            cursor_col = 0;
            cursor_row = FIRST_TEXT_ROW;
        }
        return true;
    }
    return false;
}

void console_putc(char c) {
    if (!ready) {
        return;
    }
    draw_cursor(false);
    if (!handle_escape(c)) {
        putc_raw(c);
    }
    draw_cursor(true);
}

void console_write(const char *s) {
    while (*s) {
        console_putc(*s++);
    }
}

void console_clear(void) {
    if (!ready) {
        return;
    }
    const struct framebuffer *fb = fb_get();
    fb_fill_rect(0, FIRST_TEXT_ROW * glyph_height, fb->width,
                 fb->height - FIRST_TEXT_ROW * glyph_height, bg_color);
    cursor_col = 0;
    cursor_row = FIRST_TEXT_ROW;
    draw_cursor(true);
}

void console_set_status(const char *text) {
    if (!ready) {
        return;
    }
    size_t len = 0;
    while (text[len] != '\0') {
        len++;
    }
    if (len > cols) {
        len = cols;
    }
    // Right-align, fill the rest of the bar with spaces.
    uint64_t start = cols - len;
    for (uint64_t col = 0; col < cols; col++) {
        char c = col >= start ? text[col - start] : ' ';
        draw_glyph((uint8_t)c, col, STATUS_ROW, status_fg, status_bg);
    }
}
