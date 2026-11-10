// Tests for Stage 7: user mode and system calls.

#include <stdint.h>
#include <stdbool.h>
#include "tests/tests.h"
#include "tests/ktest.h"
#include "lib/kprintf.h"
#include "sched/task.h"
#include "proc/process.h"
#include "proc/programs.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "mm/vmm.h"

// Start a built-in program and wait (sleeping) until its task is gone.
static bool run_program(const char *name) {
    const struct builtin_program *p = program_find(name);
    if (p == NULL) {
        kprintf("no program '%s'\n", name);
        return false;
    }
    struct task *t = process_spawn_flat(name, p->start, (size_t)(p->end - p->start));
    if (t == NULL) {
        kprintf("could not start '%s'\n", name);
        return false;
    }
    uint64_t pid = t->id;   // t is freed when the process ends: keep only the id
    while (task_exists(pid)) {
        task_sleep_ms(10);
    }
    return true;
}

void test_user_hello(void) {
    ktest_finish(run_program("hello"));
}

// Done test of Stage 7: user programs crash, the kernel kills them and goes on.
void test_user_crash(void) {
    bool ok = run_program("crash_null") && run_program("crash_priv")
              && run_program("crash_kernel") && run_program("hello");
    kprintf("kernel still alive after 3 crashed processes\n");
    ktest_finish(ok);
}

void test_user_badptr(void) {
    ktest_finish(run_program("badptr"));
}

void test_user_read(void) {
    ktest_finish(run_program("reader"));
}

// Start and finish a process many times: its address space (code, stack,
// page tables) and its task must all be freed again.
void test_user_leak(void) {
    run_program("hello");   // warm-up (first stack slot, first heap growth)
    uint64_t free_before = pmm_get_stats().free_pages;
    uint64_t allocs_before = heap_get_stats().allocations;
    uint64_t tables_before = vmm_table_pages();
    for (int i = 0; i < 10; i++) {
        run_program("crash_null");
        run_program("hello");
    }
    task_sleep_ms(20);
    long page_leak = (long)(free_before - pmm_get_stats().free_pages);
    long alloc_leak = (long)(heap_get_stats().allocations - allocs_before);
    long table_leak = (long)(vmm_table_pages() - tables_before);
    kprintf("20 processes: page leak %ld, heap leak %ld, page-table leak %ld\n",
            page_leak, alloc_leak, table_leak);
    ktest_finish(page_leak == 0 && alloc_leak == 0 && table_leak == 0);
}

void test_user_syscall(void) {
    ktest_finish(run_program("fastcall") && run_program("hello"));
}
