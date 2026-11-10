#include "lib/panic.h"

#include <stdarg.h>
#include "lib/kprintf.h"
#include "arch/x86_64/cpu.h"
#include "tests/ktest.h"
#include "drivers/qemu.h"

void panic_at(const char *file, int line, const char *fmt, ...) {
    // Disable interrupts first, so no interrupt handler runs on a broken kernel.
    cpu_cli();

    kprintf("\n*** KERNEL PANIC at %s:%d ***\n", file, line);

    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);

    kprintf("\nSystem halted.\n");

    // In an automatic test run, quit QEMU so the runner does not wait for a timeout.
    if (ktest_active()) {
        qemu_exit(2);
    }
    halt_forever();
}
