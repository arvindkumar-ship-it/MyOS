#pragma once

// List of all in-kernel tests: { "name", function }.
// Add new tests here and implement them in kernel/tests/test_*.c.

// Stage 1-2
void test_boot(void);
void test_kprintf(void);
void test_panic(void);

// Stage 3
void test_gdt(void);
void test_div0(void);
void test_int3(void);
void test_gp(void);
void test_pf(void);
void test_df(void);
void test_ud(void);

// Stage 4
void test_timer(void);
void test_readline(void);

#define KTEST_LIST \
    { "boot",    test_boot }, \
    { "kprintf", test_kprintf }, \
    { "panic",   test_panic }, \
    { "gdt",     test_gdt }, \
    { "div0",    test_div0 }, \
    { "int3",    test_int3 }, \
    { "gp",      test_gp }, \
    { "pf",      test_pf }, \
    { "df",      test_df }, \
    { "ud",      test_ud }, \
    { "timer",   test_timer }, \
    { "keyboard", test_readline }, \
    { "serialin", test_readline },
