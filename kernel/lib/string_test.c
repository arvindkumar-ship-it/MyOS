// Self test for kernel/lib/string.c.
// Kept in a separate file on purpose: if the test were in string.c, gcc would
// inline memcpy/memmove/... into it and work out most results at compile time,
// so the real functions would never be called.

#include <stdint.h>
#include "lib/string.h"
#include "lib/panic.h"

void string_selftest(void) {
    // strlen
    kassert(strlen("") == 0);
    kassert(strlen("MyOS") == 4);

    // memset + memcmp
    uint8_t a[8], b[8];
    memset(a, 0xAB, sizeof(a));
    for (size_t i = 0; i < sizeof(a); i++) {
        kassert(a[i] == 0xAB);
    }
    memcpy(b, a, sizeof(a));
    kassert(memcmp(a, b, sizeof(a)) == 0);
    b[3] = 0xAC;                              // b is now bigger at index 3
    kassert(memcmp(a, b, sizeof(a)) < 0);
    kassert(memcmp(b, a, sizeof(a)) > 0);
    kassert(memcmp(a, b, 3) == 0);            // first 3 bytes still equal

    // memmove with overlap, both directions
    char fwd[] = "123456789";
    memmove(fwd + 2, fwd, 5);                 // dest after src: must copy backwards
    kassert(memcmp(fwd, "121234589", 9) == 0);

    char back[] = "123456789";
    memmove(back, back + 2, 5);               // dest before src: copy forwards
    kassert(memcmp(back, "345676789", 9) == 0);
}
