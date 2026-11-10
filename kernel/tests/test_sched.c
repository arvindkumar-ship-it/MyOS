// Tests for Stage 6: tasks, scheduler, sleep, spinlock.

#include <stdint.h>
#include <stdbool.h>
#include "tests/tests.h"
#include "tests/ktest.h"
#include "lib/kprintf.h"
#include "lib/spinlock.h"
#include "sched/task.h"
#include "drivers/pit.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "mm/vmm.h"

static volatile int finished;

// Wait (sleeping, so other tasks run) until "count" helper tasks are done.
static void wait_finished(int count) {
    while (finished < count) {
        task_sleep_ms(10);
    }
}

// --- Done test: A and B take turns ---

static void turn_thread(void *arg) {
    const char *letter = arg;
    for (int i = 1; i <= 5; i++) {
        kprintf("%s%d ", letter, i);
        task_yield();   // let the other one print
    }
    finished++;
}

static void sleepy_thread(void *arg) {
    const char *letter = arg;
    for (int i = 1; i <= 5; i++) {
        kprintf("%s%d ", letter, i);
        task_sleep_ms(100);
    }
    finished++;
}

void test_threads(void) {
    finished = 0;
    kprintf("yield: ");
    task_create("A", turn_thread, "A");
    task_create("B", turn_thread, "B");
    wait_finished(2);
    kprintf("\n");

    finished = 0;
    kprintf("sleep: ");
    task_create("A", sleepy_thread, "A");
    task_create("B", sleepy_thread, "B");
    wait_finished(2);
    kprintf("\n");
    ktest_finish(true);
}

// --- Preemption: two tasks that never give up the CPU still both run ---

static volatile uint64_t spin_count[2];
static volatile bool stop_spinning;

static void spinner(void *arg) {
    int id = (int)(uint64_t)arg;
    while (!stop_spinning) {
        spin_count[id]++;
    }
    finished++;
}

void test_preempt(void) {
    finished = 0;
    stop_spinning = false;
    uint64_t switches = sched_switch_count();
    task_create("spin0", spinner, (void *)0);
    task_create("spin1", spinner, (void *)1);
    task_sleep_ms(1000);
    stop_spinning = true;
    wait_finished(2);
    switches = sched_switch_count() - switches;
    kprintf("preempt: spin0 %lu loops, spin1 %lu loops, %lu context switches in 1 s\n",
            spin_count[0], spin_count[1], switches);
    // 1 s / 50 ms slices = about 20 switches per busy task.
    ktest_finish(spin_count[0] > 0 && spin_count[1] > 0 && switches >= 20);
}

// --- sleep(ms): wake order and timing ---

static volatile int wake_order[3];
static volatile int wake_index;

static void sleeper(void *arg) {
    uint64_t ms = (uint64_t)arg;
    uint64_t start = pit_ticks();
    task_sleep_ms(ms);
    uint64_t slept_ms = (pit_ticks() - start) * (1000 / PIT_HZ);
    kprintf("slept %lu ms (asked %lu)\n", slept_ms, ms);
    wake_order[wake_index++] = (int)ms;
    finished++;
}

void test_sleep(void) {
    finished = 0;
    wake_index = 0;
    kprintf("sleep test start\n");
    task_create("s300", sleeper, (void *)300);
    task_create("s100", sleeper, (void *)100);
    task_create("s200", sleeper, (void *)200);
    wait_finished(3);
    kprintf("wake order: %d %d %d\n", wake_order[0], wake_order[1], wake_order[2]);

    uint64_t start = pit_ticks();
    task_sleep_ms(1000);
    kprintf("main slept 1000 ms: %lu ticks\n", pit_ticks() - start);
    ktest_finish(wake_order[0] == 100 && wake_order[1] == 200 && wake_order[2] == 300);
}

// --- Race condition, and the spinlock that fixes it ---
//
// Two tasks do "counter = counter + 1" for 1 second, as a separate read and
// write with some work in between. When the timer switches tasks between the
// read and the write, the other task's updates in the meantime are lost.
// Each task also counts its own increments, so we know the right total.

static volatile uint64_t shared_counter;
static volatile uint64_t own_count[2];
static struct spinlock counter_lock = SPINLOCK_INIT("counter");
static volatile bool use_lock;
static volatile bool stop_racing;

static void racer(void *arg) {
    int id = (int)(uint64_t)arg;
    while (!stop_racing) {
        if (use_lock) {
            spin_lock(&counter_lock);
        }
        uint64_t value = shared_counter;
        for (volatile int d = 0; d < 50; d++) {
        }
        shared_counter = value + 1;
        if (use_lock) {
            spin_unlock(&counter_lock);
        }
        own_count[id]++;
    }
    finished++;
}

static void race_round(bool locked, uint64_t *result, uint64_t *expected) {
    finished = 0;
    shared_counter = 0;
    own_count[0] = own_count[1] = 0;
    use_lock = locked;
    stop_racing = false;
    task_create("racer0", racer, (void *)0);
    task_create("racer1", racer, (void *)1);
    task_sleep_ms(1000);
    stop_racing = true;
    wait_finished(2);
    *result = shared_counter;
    *expected = own_count[0] + own_count[1];
}

void test_race(void) {
    uint64_t result, expected;
    race_round(false, &result, &expected);
    kprintf("race without lock: counter %lu, increments %lu, lost %lu updates\n",
            result, expected, expected - result);
    bool lost_some = result < expected;
    race_round(true, &result, &expected);
    kprintf("race with spinlock: counter %lu, increments %lu, lost %lu updates\n",
            result, expected, expected - result);
    ktest_finish(lost_some && result == expected);
}

void test_deadlock(void) {
    static struct spinlock lock = SPINLOCK_INIT("test_lock");
    spin_lock(&lock);
    kprintf("taking the same spinlock again...\n");
    spin_lock(&lock);
    ktest_finish(false);
}

// --- Kernel stack overflow hits the guard page ---

static volatile bool keep_recursing = true;

static uint64_t recurse(uint64_t n) {
    volatile uint8_t buffer[256];   // use stack quickly
    buffer[0] = (uint8_t)n;
    if (!keep_recursing) {          // always true; stops gcc from optimising the recursion away
        return 0;
    }
    return recurse(n + 1) + buffer[0];
}

static void overflow_thread(void *arg) {
    (void)arg;
    kprintf("recursing forever on a 16 KiB stack...\n");
    recurse(0);
}

void test_stack_overflow(void) {
    task_create("overflow", overflow_thread, NULL);
    task_sleep_ms(1000);
    ktest_finish(false);
}

// --- 100 tasks start and finish: stacks and structs must all be freed ---

static void short_task(void *arg) {
    (void)arg;
    finished++;
}

static void run_short_tasks(int count) {
    finished = 0;
    for (int i = 0; i < count; i++) {
        if (task_create("short", short_task, NULL) == NULL) {
            kprintf("task_create failed at %d\n", i);
            ktest_finish(false);
        }
    }
    wait_finished(count);
    task_sleep_ms(20);   // let the last zombie be reaped
}

void test_task_leak(void) {
    // Warm-up round: 100 stacks use 100 stack slots, and the page tables for
    // those slots stay allocated (empty tables are not freed). Measure after.
    uint64_t tables_start = vmm_table_pages();
    run_short_tasks(100);
    kprintf("warm-up: %lu new page-table pages for stack slots\n",
            vmm_table_pages() - tables_start);

    uint64_t free_before = pmm_get_stats().free_pages;
    uint64_t allocs_before = heap_get_stats().allocations;
    run_short_tasks(100);
    long page_leak = (long)(free_before - pmm_get_stats().free_pages);
    long alloc_leak = (long)(heap_get_stats().allocations - allocs_before);
    kprintf("100 tasks done: page leak %ld, heap allocation leak %ld\n", page_leak, alloc_leak);
    ktest_finish(page_leak == 0 && alloc_leak == 0);
}
