// Tests for Stage 1 and 2: boot, kprintf, string helpers, panic.

#include <stdint.h>
#include <stddef.h>
#include "tests/tests.h"
#include "tests/ktest.h"
#include "lib/kprintf.h"
#include "lib/string.h"
#include "lib/panic.h"

void test_boot(void) {
    kprintf("Hello from MyOS\n");
    ktest_finish(true);
}

void test_kprintf(void) {
    string_selftest();
    kprintf("string selftest: ok\n");

    // Done test of Stage 2: must print "deadbeef".
    kprintf("%x\n", 0xDEADBEEF);

    // volatile: hide the NULL from gcc's compile-time format check; we test it on purpose.
    const char *volatile null_str = NULL;
    // The test runner compares these lines with the expected text.
    kprintf("[%d] [%i] [%u] [%c] [%s] [%s] [%%] [%X]\n",
            -42, 0, 4294967295u, 'A', "str", null_str, 0xBEEFu);
    kprintf("[%5d] [%05d] [%08x] [%016lx] [%ld] [%p]\n",
            -42, -42, 0x1234u, 0xffffffff80000000ul, (long)INT64_MIN, (void *)0x1000);
    ktest_finish(true);
}

void test_panic(void) {
    panic("test %d", 42);
}
