#pragma once

#include <stdbool.h>

// Text console on the framebuffer, using the built-in PSF1 bitmap font.
// Call fb_init() first.

// Check the font and reset the cursor. Returns false if the font is not valid PSF1.
bool console_init(void);

void console_putc(char c);
void console_write(const char *s);
