#include "arch/x86_64/gdt.h"

#include <stddef.h>
#include "lib/string.h"
#include "lib/kprintf.h"

// One GDT entry is 8 bytes:
//   bits  0-15  limit 0-15
//   bits 16-39  base 0-23
//   bits 40-47  access byte: P(present) DPL(2 bits) S(code/data) type(4 bits)
//   bits 48-51  limit 16-19
//   bits 52-55  flags: G(4 KiB granularity) D/B(32-bit) L(64-bit code) AVL
//   bits 56-63  base 24-31
static uint64_t gdt_entry(uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    uint64_t e = 0;
    e |= (uint64_t)(limit & 0xFFFF);
    e |= (uint64_t)(base & 0xFFFFFF) << 16;
    e |= (uint64_t)access << 40;
    e |= (uint64_t)((limit >> 16) & 0xF) << 48;
    e |= (uint64_t)(flags & 0xF) << 52;
    e |= (uint64_t)((base >> 24) & 0xFF) << 56;
    return e;
}

// Access bytes.
#define ACCESS_KERNEL_CODE 0x9A   // present, ring 0, code, execute/read
#define ACCESS_KERNEL_DATA 0x92   // present, ring 0, data, read/write
#define ACCESS_USER_CODE   0xFA   // present, ring 3, code, execute/read
#define ACCESS_USER_DATA   0xF2   // present, ring 3, data, read/write
#define ACCESS_TSS         0x89   // present, ring 0, system: 64-bit TSS (available)

// Flags nibble.
#define FLAGS_CODE64 0xA          // G=1, L=1 (64-bit code; D must be 0)
#define FLAGS_DATA   0xC          // G=1, D/B=1 (ignored in 64-bit mode)

// 5 normal entries + TSS (2 slots).
static uint64_t gdt[7] __attribute__((aligned(16)));

static struct tss tss __attribute__((aligned(16)));

// Stack for the double fault handler (see IST in gdt.h). A double fault often
// happens because the normal stack is broken, so it must have its own.
static uint8_t double_fault_stack[16 * 1024] __attribute__((aligned(16)));

// The value lgdt reads: size - 1 and VIRTUAL address of the table.
struct gdt_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// In gdt_load.asm.
extern void gdt_load(const struct gdt_pointer *gdtr, uint16_t code_sel, uint16_t data_sel);

void gdt_init(void) {
    gdt[0] = 0;   // the CPU requires entry 0 to be null
    gdt[GDT_KERNEL_CODE / 8] = gdt_entry(0, 0xFFFFF, ACCESS_KERNEL_CODE, FLAGS_CODE64);
    gdt[GDT_KERNEL_DATA / 8] = gdt_entry(0, 0xFFFFF, ACCESS_KERNEL_DATA, FLAGS_DATA);
    gdt[GDT_USER_DATA / 8]   = gdt_entry(0, 0xFFFFF, ACCESS_USER_DATA, FLAGS_DATA);
    gdt[GDT_USER_CODE / 8]   = gdt_entry(0, 0xFFFFF, ACCESS_USER_CODE, FLAGS_CODE64);

    // TSS descriptor: 16 bytes. Low 8 bytes look like a normal entry,
    // high 8 bytes hold bits 32-63 of the base address.
    memset(&tss, 0, sizeof(tss));
    tss.ist[IST_DOUBLE_FAULT - 1] = (uint64_t)(double_fault_stack + sizeof(double_fault_stack));
    tss.iomap_base = sizeof(tss);   // no I/O bitmap: user code can never use in/out

    uint64_t tss_base = (uint64_t)&tss;
    gdt[GDT_TSS / 8]     = gdt_entry((uint32_t)tss_base, sizeof(tss) - 1, ACCESS_TSS, 0);
    gdt[GDT_TSS / 8 + 1] = tss_base >> 32;

    struct gdt_pointer gdtr = {
        .limit = sizeof(gdt) - 1,
        .base = (uint64_t)gdt,
    };
    gdt_load(&gdtr, GDT_KERNEL_CODE, GDT_KERNEL_DATA);

    // Load the Task Register with the TSS selector.
    asm volatile ("ltr %0" : : "r"((uint16_t)GDT_TSS));
}

void tss_set_rsp0(uint64_t virt_stack_top) {
    tss.rsp[0] = virt_stack_top;
}
