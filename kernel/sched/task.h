#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "mm/vmm.h"

// A task = one thread of execution with its own kernel stack.
// (Stage 7 adds user processes: a task with its own address space.)

enum task_state {
    TASK_READY,      // waiting in the run queue for the CPU
    TASK_RUNNING,    // on the CPU right now
    TASK_SLEEPING,   // sleep(ms): waits for a tick count
    TASK_BLOCKED,    // waits in a wait queue (e.g. for keyboard input)
    TASK_ZOMBIE,     // finished; waiting to be cleaned up
};

#define TASK_NAME_LEN 16

struct task {
    uint64_t id;
    char name[TASK_NAME_LEN];
    enum task_state state;

    uint64_t saved_rsp;           // kernel stack pointer while not running (see switch.asm)
    uint64_t virt_kstack_bottom;  // lowest mapped address of the kernel stack (0 = boot stack)
    uint64_t virt_kstack_top;     // first address above the stack
    struct address_space *as;     // page tables used while this task runs

    uint64_t wake_tick;           // TASK_SLEEPING: wake up at this pit_ticks() value
    uint64_t ticks_used;          // timer ticks spent running (for ps)
    int exit_code;

    struct task *next;            // run queue / wait queue / sleep list link
    struct task *all_next;        // list of all tasks
};

// Kernel stack size of each task (guard page below it is left unmapped).
#define KSTACK_SIZE (16 * 1024)

// Make the running code (kmain) task 0 and create the idle task.
// After this, tasks can be created; the timer starts switching once
// sched_start_preemption() is called.
void sched_init(void);

// Turn on preemption: the timer IRQ switches tasks every time slice.
void sched_start_preemption(void);

// New kernel thread running fn(arg). Returns NULL if out of memory.
struct task *task_create(const char *name, void (*fn)(void *), void *arg);

// The task running on the CPU.
struct task *task_current(void);

// Give up the CPU; the task stays ready and runs again later.
void task_yield(void);

// Sleep for at least "ms" milliseconds (other tasks run meanwhile).
void task_sleep_ms(uint64_t ms);

// End the current task.
__attribute__((noreturn))
void task_exit(int code);

// --- Wait queues: block a task until some event happens. ---

struct wait_queue {
    struct task *head;
};

#define WAIT_QUEUE_INIT { .head = 0 }

// Block the current task on wq until wait_queue_wake_all(wq).
// Must be called with interrupts DISABLED (so the event cannot happen between
// checking the condition and going to sleep); they are disabled again on return.
void wait_queue_sleep(struct wait_queue *wq);

// Make every task waiting on wq ready again (safe from IRQ handlers).
void wait_queue_wake_all(struct wait_queue *wq);

// --- Info for ps / tests ---

// Call fn for every task (interrupts are off while it runs: keep it short).
void task_for_each(void (*fn)(struct task *t, void *ctx), void *ctx);

const char *task_state_name(enum task_state s);

// Number of context switches so far.
uint64_t sched_switch_count(void);

// True after sched_init().
bool sched_running(void);
