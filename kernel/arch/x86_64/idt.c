#include "arch/x86_64/idt.h"

#include "arch/x86_64/gdt.h"

// One IDT entry (16 bytes in 64-bit mode). The handler address is split
// into 3 parts for historical reasons.
struct idt_entry {
    uint16_t offset_low;    // handler address bits 0-15
    uint16_t selector;      // code segment to run the handler in (kernel code)
    uint8_t  ist;           // bits 0-2: IST slot, 0 = use the normal stack
    uint8_t  attributes;    // P(1) DPL(2) 0 type(4)
    uint16_t offset_mid;    // bits 16-31
    uint32_t offset_high;   // bits 32-63
    uint32_t reserved;
} __attribute__((packed));

struct idt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES] __attribute__((aligned(16)));

// Addresses of the 256 stubs, from isr.asm.
extern const uint64_t isr_stub_table[IDT_ENTRIES];

#define ATTR_PRESENT 0x80

void idt_set_gate(uint8_t vector, uint8_t ist, uint8_t dpl, uint8_t type) {
    uint64_t handler = isr_stub_table[vector];
    struct idt_entry *e = &idt[vector];
    e->offset_low  = handler & 0xFFFF;
    e->offset_mid  = (handler >> 16) & 0xFFFF;
    e->offset_high = handler >> 32;
    e->selector    = GDT_KERNEL_CODE;
    e->ist         = ist & 0x7;
    e->attributes  = ATTR_PRESENT | (uint8_t)((dpl & 3) << 5) | (type & 0xF);
    e->reserved    = 0;
}

void idt_set_present(uint8_t vector, bool present) {
    if (present) {
        idt[vector].attributes |= ATTR_PRESENT;
    } else {
        idt[vector].attributes &= (uint8_t)~ATTR_PRESENT;
    }
}

void idt_init(void) {
    for (int v = 0; v < IDT_ENTRIES; v++) {
        // Interrupt gates everywhere: the CPU clears IF on entry, so a handler
        // is never interrupted by another IRQ (simple and safe for now).
        idt_set_gate((uint8_t)v, 0, 0, IDT_TYPE_INTERRUPT);
    }

    // Double fault runs on its own known-good stack (IST1, set up in gdt.c).
    idt_set_gate(8, IST_DOUBLE_FAULT, 0, IDT_TYPE_INTERRUPT);

    // Breakpoint (int3) may be used from user mode too (debuggers).
    idt_set_gate(3, 0, 3, IDT_TYPE_INTERRUPT);

    struct idt_pointer idtr = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)idt,
    };
    asm volatile ("lidt %0" : : "m"(idtr));
}
