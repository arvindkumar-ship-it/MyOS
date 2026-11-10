#pragma once

#include <stdint.h>
#include <stdbool.h>

// Global Descriptor Table.
//
// In 64-bit mode segmentation is mostly switched off (base 0, no limit), but
// the CPU still needs segment descriptors to know two things:
//   - the privilege level of the running code (ring 0 = kernel, ring 3 = user)
//   - where the TSS is (the TSS holds the kernel stack used on interrupts)
//
// The order is fixed by syscall/sysret (Stage 7):
//   syscall loads CS = STAR[47:32],      SS = STAR[47:32] + 8
//   sysret  loads SS = STAR[63:48] + 8,  CS = STAR[63:48] + 16
// so kernel data must follow kernel code, and user data must come before user code.

#define GDT_NULL         0x00
#define GDT_KERNEL_CODE  0x08
#define GDT_KERNEL_DATA  0x10
#define GDT_USER_DATA    0x18
#define GDT_USER_CODE    0x20
#define GDT_TSS          0x28   // TSS descriptor is 16 bytes: uses slots 0x28 and 0x30

// Selector value to load for ring 3 = index | RPL 3.
#define USER_DATA_SELECTOR (GDT_USER_DATA | 3)
#define USER_CODE_SELECTOR (GDT_USER_CODE | 3)

// Task State Segment (64-bit). In long mode it does not switch tasks any more;
// it only stores stack pointers the CPU loads on a privilege change or for IST.
struct tss {
    uint32_t reserved0;
    uint64_t rsp[3];       // rsp[0]: kernel stack used when an interrupt comes from ring 3
    uint64_t reserved1;
    uint64_t ist[7];       // Interrupt Stack Table: IST1..IST7, known-good stacks
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;   // offset of the I/O permission bitmap (none: = sizeof(tss))
} __attribute__((packed));

// IST slot used by the double fault handler (1-based, as in the IDT entry).
#define IST_DOUBLE_FAULT 1

// Build and load our GDT and TSS, reload all segment registers.
void gdt_init(void);

// True if a VIRTUAL address lies inside the double fault (IST1) stack.
bool gdt_on_double_fault_stack(uint64_t virt_addr);

// Set the kernel stack the CPU switches to when an interrupt or exception
// arrives while running in ring 3. (Each task gets its own, Stage 7.)
void tss_set_rsp0(uint64_t virt_stack_top);
