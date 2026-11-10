#pragma once

#include <stddef.h>

// Basic memory helpers. There is no libc in the kernel, so we write our own.
// GCC may also emit calls to these by itself (e.g. for struct copies),
// so they must always exist, even if our code never calls them directly.

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *dest, int value, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);

// Number of characters before the terminating '\0'.
size_t strlen(const char *s);

// Check the functions above with known inputs. Panics on the first wrong result.
void string_selftest(void);
