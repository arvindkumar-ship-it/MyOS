#pragma once

// List of all in-kernel tests: { "name", function }.
// Add new tests here and implement them in kernel/tests/test_*.c.

// Stage 1-2
void test_boot(void);
void test_kprintf(void);
void test_panic(void);

// Stage 3
void test_gdt(void);

#define KTEST_LIST \
    { "boot",    test_boot }, \
    { "kprintf", test_kprintf }, \
    { "panic",   test_panic }, \
    { "gdt",     test_gdt },
