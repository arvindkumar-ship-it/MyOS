#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "mm/vmm.h"

struct file;

// Open files per task: fds[i] is file descriptor i (NULL = free).
#define TASK_MAX_FDS 16

// A task = one thread of execution with its own kernel stack.
// (Stage 7 adds user processes: a task with its own address space.)

struct task;

// A list of tasks blocked until some event (see wait_queue_sleep below).
struct wait_queue {
    struct task *head;
};

#define WAIT_QUEUE_INIT { .head = 0 }

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

    bool is_user;                 // user process: runs in ring 3 with its own address space
    uint64_t user_entry;          // user mode: first instruction (VIRTUAL, user half)
    uint64_t user_stack_top;      // user mode: initial stack pointer

    struct file *fds[TASK_MAX_FDS];   // open files (user processes)

    struct task *parent;          // waits for us with task_wait() (NULL: nobody)
    struct wait_queue children_wq;   // we sleep here in task_wait() until a child exits
    bool resources_freed;         // zombie: stack + address space already given back

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

// New user process task: will run in ring 3 at "entry" with stack "user_rsp",
// using address space "as" (the task owns it and frees it when it dies).
// The task is NOT started: the caller sets up more (open files...) first,
// then calls task_start().
struct task *task_create_user(const char *name, struct address_space *as,
                              uint64_t entry, uint64_t user_rsp);

// Put a task created with task_create_user() into the run queue.
void task_start(struct task *t);

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

// Wait until child "pid" exits; store its exit code. Frees the child's task.
// Returns pid, or -1 if pid is not a child of the current task.
int64_t task_wait(uint64_t pid, int *exit_code);

// True while a task with this id exists (also as a zombie not yet freed).
bool task_exists(uint64_t id);

// Number of context switches so far.
uint64_t sched_switch_count(void);

// Top of the current task's kernel stack. The syscall entry code switches to
// it (syscall does not switch stacks by itself, unlike interrupts).
extern uint64_t sched_kernel_rsp;

// True after sched_init().
bool sched_running(void);
