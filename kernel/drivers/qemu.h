#pragma once

#include <stdint.h>

// QEMU "isa-debug-exit" device (added by the Makefile at I/O port 0xF4).
// Writing a value V to it makes QEMU quit with exit status (V << 1) | 1.
// Used by the automatic tests so QEMU stops by itself. On a machine without
// the device the write does nothing.

#define QEMU_EXIT_PORT 0xF4

__attribute__((noreturn))
void qemu_exit(uint32_t value);
