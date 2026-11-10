#pragma once

// Small CPU helpers.

// Stop the CPU forever.
// cli: ignore maskable interrupts, so nothing can wake us up and run code.
// hlt: sleep until the next interrupt. An NMI can still wake the CPU,
// so we loop and halt again.
__attribute__((noreturn))
static inline void halt_forever(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}
