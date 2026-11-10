#pragma once

#include <stdarg.h>
#include <stddef.h>

// printf family, implemented over write(). Supported: %d %i %u %x %X %p %s %c %%,
// l/ll, width, '0' and '-' flags (no floating point).

int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int dprintf(int fd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
int putchar(int c);
int puts(const char *s);   // adds '\n'
