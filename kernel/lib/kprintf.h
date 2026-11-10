#pragma once

#include <stdarg.h>

// Kernel printf. Output always goes to serial (COM1), and also to the
// screen once the console is ready.
//
// Supported: %d %i %u %x %X %p %s %c %%
//   length:  l, ll  (64-bit: %ld %lu %lx ...)
//   width:   %8d, %08x, %016lx  (pad with spaces, or zeros with a leading 0)

__attribute__((format(printf, 1, 2)))
void kprintf(const char *fmt, ...);

// Same, but takes a va_list (used by panic and other wrappers).
void kvprintf(const char *fmt, va_list args);
