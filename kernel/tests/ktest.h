#pragma once

#include <stdbool.h>

// In-kernel tests, chosen with "test=<name>" on the kernel command line.
// tools/qemu-test.py boots the kernel with that command line, reads the
// serial output and checks it. Normal boots (no "test=") run no test.

// Read "test=" from the command line. Call once, early.
void ktest_init(const char *cmdline);

// True if the kernel was booted with "test=...".
bool ktest_active(void);

// Run the selected test (if any). Called by kmain after all init is done.
// A test either calls ktest_finish() itself, or is expected to crash/panic
// in a way the test runner checks for.
void ktest_run(void);

// Print the final marker the test runner looks for, and quit QEMU.
__attribute__((noreturn))
void ktest_finish(bool passed);
