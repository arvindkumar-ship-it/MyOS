#pragma once

#include <stdarg.h>
#include <stddef.h>

// Kernel printf. Output always goes to serial (COM1), and also to the
// screen once the console is ready.
//
// Supported: %d %i %u %x %X %p %s %c %%
//   length:  l, ll  (64-bit: %ld %lu %lx ...)
//   width:   %8d, %08x, %016lx  (pad with spaces, or zeros with a leading 0)
//   flag:    %-10s  (left-align: pad on the right)

__attribute__((format(printf, 1, 2)))
void kprintf(const char *fmt, ...);

// Same, but takes a va_list (used by panic and other wrappers).
void kvprintf(const char *fmt, va_list args);

// Format into buf (at most size-1 characters + '\0').
// Returns the length the full text would have (like C's snprintf).
__attribute__((format(printf, 3, 4)))
int ksnprintf(char *buf, size_t size, const char *fmt, ...);

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list args);
