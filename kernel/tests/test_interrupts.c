// Tests for Stage 3: GDT/TSS and CPU exceptions.

#include <stdint.h>
#include "tests/tests.h"
#include "tests/ktest.h"
#include "lib/kprintf.h"
#include <stdbool.h>
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

// ---------------------------------------------------------------------------
// CPU exceptions
// ---------------------------------------------------------------------------

#include "arch/x86_64/idt.h"

// Done test of Stage 3: 1/0 must print "Divide error at RIP=..." and not reboot.
void test_div0(void) {
    // Both volatile: gcc must not know either value at compile time.
    // With a constant 1 on top, gcc rewrites "1 / x" without any div
    // instruction (the result can only be non-zero for x = 1 or -1),
    // so the CPU would never see a division at all.
    volatile int one = 1;
    volatile int zero = 0;
    kprintf("dividing by zero...\n");
    int result = one / zero;
    kprintf("not reached: %d\n", result);
    ktest_finish(false);
}

// int3 returns to the next instruction. All registers must be unchanged after
// iretq: this catches a wrong push/pop order in isr.asm.
void test_int3(void) {
    uint64_t rax = 0x1111, rbx = 0x2222, rcx = 0x3333, rdx = 0x4444, rsi = 0x5555, rdi = 0x6666;
    register uint64_t r8  asm("r8")  = 0x8888;
    register uint64_t r9  asm("r9")  = 0x9999;
    register uint64_t r10 asm("r10") = 0xAAAA;
    register uint64_t r11 asm("r11") = 0xBBBB;
    register uint64_t r12 asm("r12") = 0xCCCC;
    register uint64_t r13 asm("r13") = 0xDDDD;
    register uint64_t r14 asm("r14") = 0xEEEE;
    register uint64_t r15 asm("r15") = 0xFFFF;

    asm volatile ("int3"
                  : "+a"(rax), "+b"(rbx), "+c"(rcx), "+d"(rdx), "+S"(rsi), "+D"(rdi),
                    "+r"(r8), "+r"(r9), "+r"(r10), "+r"(r11),
                    "+r"(r12), "+r"(r13), "+r"(r14), "+r"(r15)
                  :
                  : "memory");

    bool ok = rax == 0x1111 && rbx == 0x2222 && rcx == 0x3333 && rdx == 0x4444
           && rsi == 0x5555 && rdi == 0x6666 && r8 == 0x8888 && r9 == 0x9999
           && r10 == 0xAAAA && r11 == 0xBBBB && r12 == 0xCCCC && r13 == 0xDDDD
           && r14 == 0xEEEE && r15 == 0xFFFF;
    kprintf("back from int3, registers %s\n", ok ? "preserved" : "CORRUPTED");
    ktest_finish(ok);
}

// Loading a selector past the end of the GDT -> #GP with that selector as error code.
void test_gp(void) {
    kprintf("loading bad selector 0x80 into DS...\n");
    asm volatile ("mov %0, %%ds" : : "r"((uint16_t)0x80));
    ktest_finish(false);
}

// Reading an address nothing is mapped at -> #PF, CR2 = that address.
void test_pf(void) {
    volatile uint64_t *virt_bad = (volatile uint64_t *)0x00000deadbeef000ul;
    kprintf("reading %p...\n", (void *)virt_bad);
    uint64_t value = *virt_bad;
    kprintf("not reached: %lx\n", value);
    ktest_finish(false);
}

// Double fault: make the #PF gate "not present", then cause a page fault.
// The CPU cannot deliver the #PF (gate missing -> #NP while delivering #PF),
// and two faults in a row like that become a #DF, which runs on IST1.
void test_df(void) {
    idt_set_present(14, false);
    kprintf("page fault with #PF gate removed...\n");
    volatile uint64_t *virt_bad = (volatile uint64_t *)0x00000deadbeef000ul;
    uint64_t value = *virt_bad;
    kprintf("not reached: %lx\n", value);
    ktest_finish(false);
}

// ud2 is an instruction that is guaranteed to be invalid -> #UD.
void test_ud(void) {
    kprintf("executing ud2...\n");
    asm volatile ("ud2");
    ktest_finish(false);
}
