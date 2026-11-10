#include "drivers/input.h"

#include <stddef.h>
#include <stdint.h>
#include "lib/ringbuf.h"
#include "arch/x86_64/cpu.h"
#include "sched/task.h"

static struct ringbuf queue;

// Tasks waiting for a character (Stage 6). Woken by every input_push().
static struct wait_queue readers = WAIT_QUEUE_INIT;

void input_push(char c) {
    ringbuf_push(&queue, (uint8_t)c);   // full buffer: the character is dropped
    wait_queue_wake_all(&readers);
}

bool input_try_getc(char *c) {
    // Interrupts off while we pop, so the IRQ writer never sees a half-done pop.
    bool was_enabled = cpu_irq_save();
    uint8_t byte;
    bool got = ringbuf_pop(&queue, &byte);
    cpu_irq_restore(was_enabled);
    if (got) {
        *c = (char)byte;
    }
    return got;
}

char input_getc(void) {
    bool was_enabled = cpu_irq_save();
    uint8_t byte;
    while (!ringbuf_pop(&queue, &byte)) {
        if (sched_running()) {
            // Block this task (interrupts stay off between the check and the
            // sleep, so a key arriving right now cannot be missed).
            wait_queue_sleep(&readers);
        } else {
            // Before the scheduler exists: "sti; hlt" as a pair. sti only takes
            // effect after the next instruction, so no interrupt can slip in
            // between the check above and hlt.
            asm volatile ("sti; hlt; cli" ::: "memory");
        }
    }
    cpu_irq_restore(was_enabled);
    return (char)byte;
}
