// Tests for Stage 4: PIT timer, PS/2 keyboard, serial input.

#include <stdint.h>
#include <stdbool.h>
#include "tests/tests.h"
#include "tests/ktest.h"
#include "lib/kprintf.h"
#include "drivers/pit.h"
#include "drivers/input.h"
#include "arch/x86_64/irq.h"

// Print a line every 100 ticks. The test runner measures the real time
// between "ticks=100" and "ticks=300" (should be about 2 seconds).
void test_timer(void) {
    for (uint64_t target = 100; target <= 300; target += 100) {
        while (pit_ticks() < target) {
            asm volatile ("hlt");
        }
        kprintf("ticks=%lu\n", target);
    }
    uint64_t start = pit_ticks();
    pit_sleep_ms(500);
    uint64_t slept = pit_ticks() - start;
    kprintf("pit_sleep_ms(500) took %lu ticks\n", slept);
    kprintf("IRQ0 count: %lu\n", irq_count(0));
    ktest_finish(slept >= 50 && slept <= 52);
}

// Read one line from the input queue, echoing it, with backspace support.
void test_readline(void) {
    char line[64];
    int len = 0;
    kprintf("type a line:\n");
    for (;;) {
        char c = input_getc();
        if (c == '\n') {
            kprintf("\n");
            break;
        }
        if (c == '\b') {
            if (len > 0) {
                len--;
                kprintf("\b");
            }
            continue;
        }
        if (len < (int)sizeof(line) - 1) {
            line[len++] = c;
            kprintf("%c", c);
        }
    }
    line[len] = '\0';
    kprintf("got line: '%s'\n", line);
    ktest_finish(true);
}
