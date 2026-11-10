// Tests for Stage 3: GDT/TSS and CPU exceptions.

#include <stdint.h>
#include "tests/tests.h"
#include "tests/ktest.h"
#include "lib/kprintf.h"
#include "arch/x86_64/gdt.h"

// Read the selector currently loaded in a segment register / task register.
#define READ_SEG(reg) ({ uint16_t v; asm volatile ("mov %%" #reg ", %0" : "=r"(v)); v; })

void test_gdt(void) {
    uint16_t cs = READ_SEG(cs), ss = READ_SEG(ss), ds = READ_SEG(ds);
    uint16_t tr;
    asm volatile ("str %0" : "=r"(tr));
    kprintf("cs=%x ss=%x ds=%x tr=%x\n", cs, ss, ds, tr);
    ktest_finish(cs == GDT_KERNEL_CODE && ss == GDT_KERNEL_DATA
                 && ds == GDT_KERNEL_DATA && tr == GDT_TSS);
}
