#include "sched/task.h"

#include <stddef.h>
#include "mm/memory.h"
#include "mm/heap.h"
#include "mm/vmm.h"
#include "drivers/pit.h"
#include "lib/string.h"
#include "lib/kprintf.h"
#include "lib/panic.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/gdt.h"
#include "fs/vfs.h"

// Round-robin scheduler for one CPU.
//
// Ready tasks wait in a FIFO run queue. The timer IRQ (100 Hz) counts down the
// running task's time slice; at 0 the task goes to the back of the queue and
// the first one in the queue gets the CPU (preemption). Tasks can also give up
// the CPU themselves: yield, sleep, wait for an event, exit.
//
// All scheduler data is changed only with interrupts disabled. On one CPU that
// is enough: nothing else can run while IF = 0.

#define TIME_SLICE_TICKS 5     // 50 ms at 100 Hz
#define MAX_KSTACKS      256
#define KSTACK_SLOT      (64 * 1024)   // virtual space per stack: stack + unmapped guard below

// In switch.asm.
extern void switch_context(uint64_t *old_rsp_save, uint64_t new_rsp);
extern void task_trampoline(void);

static struct task *current;
static struct task *idle_task;
static struct task *run_head, *run_tail;   // run queue (READY tasks, FIFO)
static struct task *sleep_list;            // SLEEPING tasks, sorted by wake_tick
static struct task *all_tasks;
static struct task *last_prev;             // task we just switched away from
static uint64_t next_id;
static uint64_t slice_left;
static bool preemption;
static bool running;
static uint64_t switch_count;
static bool kstack_slot_used[MAX_KSTACKS];

uint64_t sched_kernel_rsp;

// In usermode.asm: iretq into ring 3.
extern void enter_user_mode(uint64_t entry, uint64_t user_rsp) __attribute__((noreturn));

// ---------------------------------------------------------------------------
// Kernel stacks: each one sits at the top of a 64 KiB virtual slot. The pages
// below it in the slot are never mapped, so running off the end of the stack
// hits an unmapped "guard" page and faults instead of silently overwriting
// another task's memory.
// ---------------------------------------------------------------------------

static bool kstack_alloc(struct task *t) {
    for (int i = 0; i < MAX_KSTACKS; i++) {
        if (kstack_slot_used[i]) {
            continue;
        }
        uint64_t slot = VIRT_STACKS_START + (uint64_t)i * KSTACK_SLOT;
        uint64_t bottom = slot + KSTACK_SLOT - KSTACK_SIZE;
        if (!vmm_alloc_pages(vmm_kernel_space(), bottom, KSTACK_SIZE / PAGE_SIZE,
                             PTE_WRITE | PTE_NX)) {
            return false;
        }
        kstack_slot_used[i] = true;
        t->virt_kstack_bottom = bottom;
        t->virt_kstack_top = slot + KSTACK_SLOT;
        return true;
    }
    return false;
}

static void kstack_free(struct task *t) {
    if (t->virt_kstack_bottom == 0) {
        return;   // boot stack (kmain): not ours to free
    }
    vmm_free_pages(vmm_kernel_space(), t->virt_kstack_bottom, KSTACK_SIZE / PAGE_SIZE);
    kstack_slot_used[(t->virt_kstack_bottom - VIRT_STACKS_START) / KSTACK_SLOT] = false;
}

// ---------------------------------------------------------------------------
// Queues (call with interrupts off)
// ---------------------------------------------------------------------------

static void run_queue_push(struct task *t) {
    t->next = NULL;
    if (run_tail != NULL) {
        run_tail->next = t;
    } else {
        run_head = t;
    }
    run_tail = t;
}

static struct task *run_queue_pop(void) {
    struct task *t = run_head;
    if (t != NULL) {
        run_head = t->next;
        if (run_head == NULL) {
            run_tail = NULL;
        }
        t->next = NULL;
    }
    return t;
}

static void make_ready(struct task *t) {
    t->state = TASK_READY;
    if (t != idle_task) {
        run_queue_push(t);
    }
}

// Remove a task from the all-tasks list and free its struct.
static void free_task_struct(struct task *t) {
    struct task **pp = &all_tasks;
    while (*pp != NULL && *pp != t) {
        pp = &(*pp)->all_next;
    }
    if (*pp == t) {
        *pp = t->all_next;
    }
    kfree(t);
}

// Free what a finished task used: kernel stack and address space. The struct
// (with the exit code) stays while a parent may still wait() for it.
static void release_resources(struct task *t) {
    kstack_free(t);
    if (t->is_user) {
        vmm_destroy_user_space(t->as);
        t->as = NULL;
    }
    t->resources_freed = true;
    if (t->parent == NULL) {
        free_task_struct(t);   // nobody will wait for it
    }
}

// Runs right after every switch, on the NEW task's stack. The old task's
// stack is not in use any more, so a finished task can be cleaned up here
// (a task can never free its own stack while it is still running on it).
static void finish_switch(void) {
    if (last_prev != NULL && last_prev->state == TASK_ZOMBIE) {
        release_resources(last_prev);
    }
    last_prev = NULL;
}

// Pick the next task and switch to it. Interrupts must be off.
static void schedule(void) {
    if (cpu_interrupts_enabled()) {
        panic("schedule() called with interrupts enabled");
    }
    struct task *prev = current;
    if (prev->state == TASK_RUNNING) {
        make_ready(prev);   // preempted or yielded: back of the queue
    }

    struct task *next = run_queue_pop();
    if (next == NULL) {
        next = idle_task;
    }
    slice_left = TIME_SLICE_TICKS;
    if (next == prev) {
        prev->state = TASK_RUNNING;
        return;
    }

    next->state = TASK_RUNNING;
    current = next;
    switch_count++;
    if (next->as != prev->as) {
        vmm_switch(next->as);
    }
    // An interrupt (or syscall) from ring 3 must land on the kernel stack of
    // the task that is running. The CPU takes that stack from TSS.rsp0.
    if (next->virt_kstack_top != 0) {
        tss_set_rsp0(next->virt_kstack_top);
        sched_kernel_rsp = next->virt_kstack_top;
    }
    last_prev = prev;
    switch_context(&prev->saved_rsp, next->saved_rsp);

    // We get here when some later schedule() switches back to "prev".
    finish_switch();
}

// Timer IRQ, 100 times a second (interrupts are off in IRQ handlers).
static void sched_tick(void) {
    uint64_t now = pit_ticks();

    // Wake sleepers whose time has come. The list is sorted, so we can stop
    // at the first one that must sleep longer.
    while (sleep_list != NULL && sleep_list->wake_tick <= now) {
        struct task *t = sleep_list;
        sleep_list = t->next;
        make_ready(t);
    }

    current->ticks_used++;
    if (!preemption) {
        return;
    }
    // Idle gives way as soon as anyone is ready; others when the slice is used up.
    if ((current == idle_task && run_head != NULL) || --slice_left == 0) {
        schedule();
    }
}

// C entry of every new task (called from task_trampoline in switch.asm).
void task_entry(void (*fn)(void *), void *arg) __attribute__((noreturn));
void task_entry(void (*fn)(void *), void *arg) {
    finish_switch();
    cpu_sti();   // we arrived here from schedule(), where interrupts were off
    fn(arg);
    task_exit(0);
}

static void idle_loop(void *arg) {
    (void)arg;
    for (;;) {
        cpu_cli();
        if (run_head != NULL) {
            schedule();
        }
        // Atomic "enable interrupts and sleep": a wakeup IRQ cannot be missed.
        asm volatile ("sti; hlt" ::: "memory");
    }
}

static struct task *task_alloc(const char *name) {
    struct task *t = kzalloc(sizeof(*t));
    if (t == NULL) {
        return NULL;
    }
    t->id = next_id++;
    size_t i = 0;
    for (; i < TASK_NAME_LEN - 1 && name[i] != '\0'; i++) {
        t->name[i] = name[i];
    }
    t->name[i] = '\0';
    t->as = vmm_kernel_space();
    return t;
}

// Create a task but do not start it yet (the caller fills in more fields).
static struct task *task_create_stopped(const char *name, void (*fn)(void *), void *arg) {
    struct task *t = task_alloc(name);
    if (t == NULL) {
        return NULL;
    }
    if (!kstack_alloc(t)) {
        kfree(t);
        return NULL;
    }

    // Build the stack exactly as switch_context leaves it for a task that is
    // not running, so the first switch to it "returns" into task_trampoline
    // with r12 = fn and r13 = arg.
    uint64_t *sp = (uint64_t *)t->virt_kstack_top;
    *--sp = (uint64_t)task_trampoline;   // return address for switch_context's ret
    *--sp = 0;                           // rbx
    *--sp = 0;                           // rbp
    *--sp = (uint64_t)fn;                // r12
    *--sp = (uint64_t)arg;               // r13
    *--sp = 0;                           // r14
    *--sp = 0;                           // r15
    t->saved_rsp = (uint64_t)sp;
    return t;
}

void task_start(struct task *t) {
    bool was_enabled = cpu_irq_save();
    t->all_next = all_tasks;
    all_tasks = t;
    make_ready(t);
    cpu_irq_restore(was_enabled);
}

struct task *task_create(const char *name, void (*fn)(void *), void *arg) {
    struct task *t = task_create_stopped(name, fn, arg);
    if (t != NULL) {
        task_start(t);
    }
    return t;
}

// Kernel-side start of every user process: we are already on its address
// space (schedule() loaded its CR3), so just drop to ring 3.
static void user_task_start(void *arg) {
    (void)arg;
    cpu_cli();   // iretq will turn interrupts on again (RFLAGS.IF in the frame)
    enter_user_mode(current->user_entry, current->user_stack_top);
}

struct task *task_create_user(const char *name, struct address_space *as,
                              uint64_t entry, uint64_t user_rsp) {
    struct task *t = task_create_stopped(name, user_task_start, NULL);
    if (t == NULL) {
        return NULL;
    }
    t->is_user = true;
    t->as = as;
    t->user_entry = entry;
    t->user_stack_top = user_rsp;
    return t;
}

void sched_init(void) {
    // The code running now (kmain, on Limine's boot stack) becomes task 0.
    struct task *boot = task_alloc("kmain");
    if (boot == NULL) {
        panic("sched_init: out of memory");
    }
    boot->state = TASK_RUNNING;
    boot->all_next = NULL;
    all_tasks = boot;
    current = boot;
    slice_left = TIME_SLICE_TICKS;

    // Idle task: runs only when nobody else is ready. It is never in the run queue.
    idle_task = task_create("idle", idle_loop, NULL);
    if (idle_task == NULL) {
        panic("sched_init: cannot create idle task");
    }
    bool was_enabled = cpu_irq_save();
    run_queue_pop();   // task_create queued it; idle is special
    idle_task->state = TASK_READY;
    cpu_irq_restore(was_enabled);

    pit_set_tick_callback(sched_tick);
    running = true;
}

void sched_start_preemption(void) {
    preemption = true;
}

struct task *task_current(void) {
    return current;
}

void task_yield(void) {
    bool was_enabled = cpu_irq_save();
    schedule();
    cpu_irq_restore(was_enabled);
}

void task_sleep_ms(uint64_t ms) {
    uint64_t ticks = (ms * PIT_HZ + 999) / 1000;   // round up
    if (ticks == 0) {
        ticks = 1;
    }
    bool was_enabled = cpu_irq_save();
    current->wake_tick = pit_ticks() + ticks;
    current->state = TASK_SLEEPING;
    // Insert sorted by wake_tick, AFTER tasks with the same wake_tick: tasks
    // that went to sleep first for the same moment also wake first (fair order).
    struct task **pp = &sleep_list;
    while (*pp != NULL && (*pp)->wake_tick <= current->wake_tick) {
        pp = &(*pp)->next;
    }
    current->next = *pp;
    *pp = current;
    schedule();
    cpu_irq_restore(was_enabled);
}

void task_exit(int code) {
    cpu_cli();
    if (current == idle_task || current->virt_kstack_bottom == 0) {
        panic("task_exit: task '%s' must not exit", current->name);
    }
    current->exit_code = code;
    // Close every open file of the task (drops its references).
    for (int i = 0; i < TASK_MAX_FDS; i++) {
        if (current->fds[i] != NULL) {
            vfs_close(current->fds[i]);
            current->fds[i] = NULL;
        }
    }
    // Our children become orphans: nobody will wait for them now.
    for (struct task *t = all_tasks; t != NULL;) {
        struct task *next = t->all_next;
        if (t->parent == current) {
            t->parent = NULL;
            if (t->state == TASK_ZOMBIE && t->resources_freed) {
                free_task_struct(t);   // finished earlier, never waited for
            }
        }
        t = next;
    }
    current->state = TASK_ZOMBIE;   // stack + memory freed by finish_switch() in the next task
    if (current->parent != NULL) {
        wait_queue_wake_all(&current->parent->children_wq);
    }
    schedule();
    panic("task_exit: zombie task ran again");
}

void wait_queue_sleep(struct wait_queue *wq) {
    if (cpu_interrupts_enabled()) {
        panic("wait_queue_sleep: interrupts must be disabled");
    }
    current->state = TASK_BLOCKED;
    current->next = NULL;
    // Append at the end, so tasks are woken in the order they started waiting.
    struct task **pp = &wq->head;
    while (*pp != NULL) {
        pp = &(*pp)->next;
    }
    *pp = current;
    schedule();
}

void wait_queue_wake_all(struct wait_queue *wq) {
    bool was_enabled = cpu_irq_save();
    struct task *t = wq->head;
    wq->head = NULL;
    while (t != NULL) {
        struct task *next = t->next;
        make_ready(t);
        t = next;
    }
    cpu_irq_restore(was_enabled);
}

void task_for_each(void (*fn)(struct task *t, void *ctx), void *ctx) {
    bool was_enabled = cpu_irq_save();
    for (struct task *t = all_tasks; t != NULL; t = t->all_next) {
        fn(t, ctx);
    }
    cpu_irq_restore(was_enabled);
}

bool task_exists(uint64_t id) {
    bool found = false;
    bool was_enabled = cpu_irq_save();
    for (struct task *t = all_tasks; t != NULL; t = t->all_next) {
        if (t->id == id) {
            found = true;
            break;
        }
    }
    cpu_irq_restore(was_enabled);
    return found;
}

int64_t task_wait(uint64_t pid, int *exit_code) {
    bool was_enabled = cpu_irq_save();
    for (;;) {
        struct task *child = NULL;
        for (struct task *t = all_tasks; t != NULL; t = t->all_next) {
            if (t->id == pid && t->parent == current) {
                child = t;
                break;
            }
        }
        if (child == NULL) {
            cpu_irq_restore(was_enabled);
            return -1;   // not our child (or already waited for)
        }
        if (child->state == TASK_ZOMBIE && child->resources_freed) {
            if (exit_code != NULL) {
                *exit_code = child->exit_code;
            }
            free_task_struct(child);
            cpu_irq_restore(was_enabled);
            return (int64_t)pid;
        }
        // Sleep until one of our children exits, then look again.
        wait_queue_sleep(&current->children_wq);
    }
}

const char *task_state_name(enum task_state s) {
    switch (s) {
    case TASK_READY:    return "ready";
    case TASK_RUNNING:  return "running";
    case TASK_SLEEPING: return "sleeping";
    case TASK_BLOCKED:  return "blocked";
    case TASK_ZOMBIE:   return "zombie";
    }
    return "?";
}

uint64_t sched_switch_count(void) {
    return switch_count;
}

bool sched_running(void) {
    return running;
}
