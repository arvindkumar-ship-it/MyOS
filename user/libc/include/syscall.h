#pragma once

#include <stdint.h>

// Raw system calls with the "syscall" instruction.
// rax = number, rdi/rsi/rdx = arguments, result in rax.
// The CPU uses rcx and r11 for the return address and flags, so they are clobbered.

// Numbers: same as kernel/syscall/syscall_nr.h (the Makefile adds kernel/syscall
// to the include path so we use the very same file).
#include "syscall_nr.h"

static inline int64_t syscall0(uint64_t n) {
    int64_t r;
    asm volatile ("syscall" : "=a"(r) : "a"(n) : "rcx", "r11", "memory");
    return r;
}

static inline int64_t syscall1(uint64_t n, uint64_t a) {
    int64_t r;
    asm volatile ("syscall" : "=a"(r) : "a"(n), "D"(a) : "rcx", "r11", "memory");
    return r;
}

static inline int64_t syscall2(uint64_t n, uint64_t a, uint64_t b) {
    int64_t r;
    asm volatile ("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b) : "rcx", "r11", "memory");
    return r;
}

static inline int64_t syscall3(uint64_t n, uint64_t a, uint64_t b, uint64_t c) {
    int64_t r;
    asm volatile ("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return r;
}
