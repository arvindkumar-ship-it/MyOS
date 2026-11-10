#pragma once

#include <stdbool.h>

// Text console on the framebuffer, using the built-in PSF1 bitmap font.
// Call fb_init() first.
//
// Row 0 is a status bar (console_set_status); text scrolls in rows 1 and below.
// Special characters: '\n' new line, '\r' start of line, '\b' erase one
// character to the left, '\f' clear the text area.
// ANSI escapes: "ESC[2J" clears, "ESC[H" moves home, others are ignored.

// Check the font and reset the cursor. Returns false if the font is not valid PSF1.
bool console_init(void);

// True after console_init() succeeded.
bool console_is_ready(void);

void console_putc(char c);
void console_write(const char *s);

// Clear the text area and move the cursor to its top-left corner.
void console_clear(void);

// Show text in the status bar (row 0), right-aligned. Does not move the cursor.
void console_set_status(const char *text);
