#include "drivers/input.h"

#include <stddef.h>
#include <stdint.h>
#include "lib/ringbuf.h"
#include "arch/x86_64/cpu.h"

static struct ringbuf queue;
static void (*push_callback)(void);

void input_push(char c) {
    ringbuf_push(&queue, (uint8_t)c);   // full buffer: the character is dropped
    if (push_callback != NULL) {
        push_callback();
    }
}

void input_set_push_callback(void (*callback)(void)) {
    push_callback = callback;
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
    for (;;) {
        cpu_cli();
        uint8_t byte;
        if (ringbuf_pop(&queue, &byte)) {
            cpu_sti();
            return (char)byte;
        }
        // "sti; hlt" as a pair: sti only takes effect after the next instruction,
        // so no interrupt can slip in between the check above and hlt. Without
        // this, a key arriving right there would leave us asleep until the next one.
        asm volatile ("sti; hlt" ::: "memory");
    }
}
