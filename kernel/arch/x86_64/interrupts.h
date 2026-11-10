#pragma once

#include <stdint.h>

// Everything isr.asm saved on the stack, in memory order (lowest address first).
// Must match the push order in isr.asm exactly.
struct interrupt_frame {
    // Pushed by isr_common (last pushed = lowest address = first here).
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    // Pushed by the stub.
    uint64_t vector;
    uint64_t error_code;
    // Pushed by the CPU.
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

typedef void (*interrupt_handler_t)(struct interrupt_frame *frame);

// Called from isr.asm for every interrupt/exception.
void interrupt_dispatch(struct interrupt_frame *frame);

// Run "handler" when interrupt "vector" happens (IRQs, syscalls, or to
// override an exception handler).
void interrupt_register(uint8_t vector, interrupt_handler_t handler);

// Print all registers of a frame (used for crash reports).
void interrupt_dump_frame(const struct interrupt_frame *frame);

// Set up GDT-independent interrupt state: build the IDT and load it.
void interrupts_init(void);
