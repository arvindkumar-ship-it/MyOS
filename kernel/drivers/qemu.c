#include "drivers/qemu.h"

#include "arch/x86_64/io.h"
#include "arch/x86_64/cpu.h"

void qemu_exit(uint32_t value) {
    outl(QEMU_EXIT_PORT, value);
    // Still here: no debug-exit device. Just stop.
    halt_forever();
}
