#include "string.h"

#include <stdint.h>

// Simple byte-by-byte versions. Slow but easy to read and correct.
// We will revisit these in Stage 2.

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void *memset(void *dest, int value, size_t n) {
    uint8_t *d = dest;
    for (size_t i = 0; i < n; i++) {
        d[i] = (uint8_t)value;
    }
    return dest;
}

// Like memcpy, but safe when dest and src overlap.
void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    if (d < s) {
        // Copy forwards: we never overwrite bytes we still need to read.
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        // Copy backwards for the same reason.
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *p = a;
    const uint8_t *q = b;
    for (size_t i = 0; i < n; i++) {
        if (p[i] != q[i]) {
            return p[i] < q[i] ? -1 : 1;
        }
    }
    return 0;
}
