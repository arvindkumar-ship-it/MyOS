#pragma once

#include <stdint.h>
#include <stdbool.h>

// Small CPU helpers: interrupts on/off, control registers, MSRs.

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

static inline void cpu_cli(void) { asm volatile ("cli" ::: "memory"); }
static inline void cpu_sti(void) { asm volatile ("sti" ::: "memory"); }
static inline void cpu_hlt(void) { asm volatile ("hlt" ::: "memory"); }

// RFLAGS bit 9 (IF) = 1 means maskable interrupts are enabled.
#define RFLAGS_IF (1ul << 9)

static inline uint64_t cpu_read_rflags(void) {
    uint64_t rflags;
    asm volatile ("pushfq; pop %0" : "=r"(rflags) : : "memory");
    return rflags;
}

static inline bool cpu_interrupts_enabled(void) {
    return (cpu_read_rflags() & RFLAGS_IF) != 0;
}

// Disable interrupts and return the old state, so it can be put back later.
// (Needed when code that already runs with interrupts off calls us.)
static inline bool cpu_irq_save(void) {
    bool was_enabled = cpu_interrupts_enabled();
    cpu_cli();
    return was_enabled;
}

static inline void cpu_irq_restore(bool was_enabled) {
    if (was_enabled) {
        cpu_sti();
    }
}

// CR2 holds the VIRTUAL address that caused the last page fault.
static inline uint64_t cpu_read_cr2(void) {
    uint64_t value;
    asm volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
}

// CR3 holds the PHYSICAL address of the top-level page table (PML4).
static inline uint64_t cpu_read_cr3(void) {
    uint64_t value;
    asm volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

static inline void cpu_write_cr3(uint64_t phys_pml4) {
    asm volatile ("mov %0, %%cr3" : : "r"(phys_pml4) : "memory");
}

// Drop the cached translation (TLB entry) for one virtual address.
static inline void cpu_invlpg(uint64_t virt_addr) {
    asm volatile ("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

// Model Specific Registers (used for syscall/sysret setup).
static inline uint64_t cpu_rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void cpu_wrmsr(uint32_t msr, uint64_t value) {
    asm volatile ("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

// CR0 bit 16 (WP): when set, ring 0 also respects read-only pages.
#define CR0_WP (1ul << 16)

static inline uint64_t cpu_read_cr0(void) {
    uint64_t value;
    asm volatile ("mov %%cr0, %0" : "=r"(value));
    return value;
}

static inline void cpu_write_cr0(uint64_t value) {
    asm volatile ("mov %0, %%cr0" : : "r"(value) : "memory");
}

// EFER MSR: bit 11 (NXE) enables the No-Execute bit in page tables.
#define MSR_EFER 0xC0000080
#define EFER_NXE (1ul << 11)
