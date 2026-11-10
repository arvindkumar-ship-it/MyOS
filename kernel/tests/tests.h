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

// Stage 5
void test_pmm(void);
void test_pmm_double_free(void);
void test_vmm(void);
void test_vmm_unmapped(void);
void test_nx(void);
void test_wp(void);
void test_null(void);
void test_heap(void);
void test_heap_double_free(void);
void test_heap_overflow(void);

// Stage 6
void test_threads(void);
void test_preempt(void);
void test_sleep(void);
void test_race(void);
void test_deadlock(void);
void test_stack_overflow(void);
void test_task_leak(void);

// Stage 7
void test_user_hello(void);
void test_user_crash(void);
void test_user_badptr(void);
void test_user_read(void);
void test_user_leak(void);
void test_user_syscall(void);

// Stage 8
void test_vfs(void);
void test_user_cat(void);

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
    { "serialin", test_readline }, \
    { "pmm",     test_pmm }, \
    { "pmm_double_free", test_pmm_double_free }, \
    { "vmm",     test_vmm }, \
    { "vmm_unmapped", test_vmm_unmapped }, \
    { "nx",      test_nx }, \
    { "wp",      test_wp }, \
    { "null",    test_null }, \
    { "heap",    test_heap }, \
    { "heap_double_free", test_heap_double_free }, \
    { "heap_overflow", test_heap_overflow }, \
    { "threads", test_threads }, \
    { "preempt", test_preempt }, \
    { "sleep",   test_sleep }, \
    { "race",    test_race }, \
    { "deadlock", test_deadlock }, \
    { "stack_overflow", test_stack_overflow }, \
    { "task_leak", test_task_leak }, \
    { "user_hello", test_user_hello }, \
    { "user_crash", test_user_crash }, \
    { "user_badptr", test_user_badptr }, \
    { "user_read", test_user_read }, \
    { "user_leak", test_user_leak }, \
    { "user_syscall", test_user_syscall }, \
    { "vfs",     test_vfs }, \
    { "user_cat", test_user_cat },
