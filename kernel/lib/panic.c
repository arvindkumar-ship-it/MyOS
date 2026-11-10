#include "lib/panic.h"

#include <stdarg.h>
#include "lib/kprintf.h"
#include "arch/x86_64/cpu.h"

void panic_at(const char *file, int line, const char *fmt, ...) {
    // Disable interrupts first, so no interrupt handler runs on a broken kernel.
    asm volatile ("cli");

    kprintf("\n*** KERNEL PANIC at %s:%d ***\n", file, line);

    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);

    kprintf("\nSystem halted.\n");
    halt_forever();
}
