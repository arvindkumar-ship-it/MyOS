#pragma once

#include <stdint.h>
#include "syscall/syscall_nr.h"

// System calls: the only way a user program can ask the kernel for something.
//
// Calling convention (same registers as Linux x86_64):
//   rax = syscall number
//   rdi, rsi, rdx, r10, r8, r9 = arguments 1-6
//   return value in rax (negative = -error code)
// Two ways in: "int 0x80" (an interrupt gate with DPL 3) and the faster
// "syscall" instruction (Stage 7, second part).

// Install the int 0x80 gate and the syscall/sysret MSRs.
void syscall_init(void);

// Run syscall "nr". Used by both entry paths.
int64_t syscall_dispatch(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5);
