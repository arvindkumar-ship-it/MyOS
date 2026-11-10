#include "arch/x86_64/interrupts.h"

#include <stddef.h>
#include "arch/x86_64/idt.h"
#include "arch/x86_64/gdt.h"
#include "mm/vmm.h"
#include "sched/task.h"
#include "arch/x86_64/cpu.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

static interrupt_handler_t handlers[256];

// Names of the 32 CPU exceptions (Intel SDM Vol. 3, table 6-1).
static const char *const exception_names[32] = {
    "Divide error",                 // 0  #DE
    "Debug",                        // 1  #DB
    "Non-maskable interrupt",       // 2  NMI
    "Breakpoint",                   // 3  #BP
    "Overflow",                     // 4  #OF
    "Bound range exceeded",         // 5  #BR
    "Invalid opcode",               // 6  #UD
    "Device not available",         // 7  #NM
    "Double fault",                 // 8  #DF
    "Coprocessor segment overrun",  // 9
    "Invalid TSS",                  // 10 #TS
    "Segment not present",          // 11 #NP
    "Stack-segment fault",          // 12 #SS
    "General protection fault",     // 13 #GP
    "Page fault",                   // 14 #PF
    "Reserved",                     // 15
    "x87 floating-point error",     // 16 #MF
    "Alignment check",              // 17 #AC
    "Machine check",                // 18 #MC
    "SIMD floating-point error",    // 19 #XM
    "Virtualization exception",     // 20 #VE
    "Control protection exception", // 21 #CP
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", // 22-27
    "Hypervisor injection",         // 28
    "VMM communication",            // 29
    "Security exception",           // 30
    "Reserved",                     // 31
};

void interrupt_register(uint8_t vector, interrupt_handler_t handler) {
    handlers[vector] = handler;
}

void interrupt_dump_frame(const struct interrupt_frame *f) {
    kprintf("  RIP=%p  CS=%lx  RFLAGS=%lx\n", (void *)f->rip, f->cs, f->rflags);
    kprintf("  RSP=%p  SS=%lx  error=%lx\n", (void *)f->rsp, f->ss, f->error_code);
    kprintf("  RAX=%016lx RBX=%016lx RCX=%016lx RDX=%016lx\n", f->rax, f->rbx, f->rcx, f->rdx);
    kprintf("  RSI=%016lx RDI=%016lx RBP=%016lx\n", f->rsi, f->rdi, f->rbp);
    kprintf("  R8 =%016lx R9 =%016lx R10=%016lx R11=%016lx\n", f->r8, f->r9, f->r10, f->r11);
    kprintf("  R12=%016lx R13=%016lx R14=%016lx R15=%016lx\n", f->r12, f->r13, f->r14, f->r15);
    kprintf("  CR2=%p  CR3=%p\n", (void *)cpu_read_cr2(), (void *)cpu_read_cr3());
}

// Explain the page fault error code bits.
static void print_page_fault(const struct interrupt_frame *f) {
    uint64_t e = f->error_code;
    kprintf("Page fault at RIP=%p: %s %s, %s mode, address CR2=%p%s%s\n",
            (void *)f->rip,
            (e & 1) ? "protection violation on" : "not-present page on",
            (e & 16) ? "instruction fetch" : (e & 2) ? "write" : "read",
            (e & 4) ? "user" : "kernel",
            (void *)cpu_read_cr2(),
            (e & 8) ? ", reserved bit set in page table" : "",
            (e & 32) ? ", protection key" : "");
}

// Explain a selector error code (#GP, #NP, #SS, #TS).
static void print_selector_error(const struct interrupt_frame *f) {
    uint64_t e = f->error_code;
    if (e == 0) {
        kprintf("  (error code 0: not caused by a segment selector)\n");
        return;
    }
    // bit 0 EXT: external event, bit 1 IDT, bit 2 TI (0 = GDT, 1 = LDT), bits 3-15 index.
    const char *table = (e & 2) ? "IDT" : (e & 4) ? "LDT" : "GDT";
    kprintf("  selector error: %s index %lu (selector %lx)%s\n",
            table, (e >> 3) & 0x1FFF, e & 0xFFF8, (e & 1) ? ", external" : "");
}

// An exception in ring 3 is the user program's fault, not the kernel's:
// report it, kill that process, and let every other task go on.
static void user_exception(struct interrupt_frame *f, const char *name) {
    struct task *t = task_current();
    kprintf("\n[process '%s' (pid %lu) killed: %s at RIP=%p",
            t->name, t->id, name, (void *)f->rip);
    if (f->vector == 14) {
        kprintf(", address %p (%s, %s)", (void *)cpu_read_cr2(),
                (f->error_code & 1) ? "protection violation" : "not present",
                (f->error_code & 16) ? "execute" : (f->error_code & 2) ? "write" : "read");
    } else if (f->error_code != 0) {
        kprintf(", error code %lx", f->error_code);
    }
    kprintf("]\n");
    // Exit code like a Unix shell shows for a signal: 128 + number.
    task_exit(128 + (int)f->vector);
}

static void exception_fatal(struct interrupt_frame *f) {
    const char *name = exception_names[f->vector];

    if ((f->cs & 3) == 3) {
        user_exception(f, name);
    }

    switch (f->vector) {
    case 0:
        // Text from the guide's Done test for Stage 3.
        kprintf("\nDivide error at RIP=%p\n", (void *)f->rip);
        break;
    case 14:
        kprintf("\n");
        print_page_fault(f);
        break;
    case 10: case 11: case 12: case 13:
        kprintf("\n%s at RIP=%p, error code %lx\n", name, (void *)f->rip, f->error_code);
        print_selector_error(f);
        break;
    case 8:
        // After a double fault the saved RIP is undefined; we are on the IST stack.
        kprintf("\nDouble fault (an exception happened while delivering another one)\n");
        kprintf("  handler running on IST1 stack: %s\n",
                gdt_on_double_fault_stack((uint64_t)f) ? "yes" : "NO");
        // Classic cause: a kernel stack overflowed into its guard page. The
        // page fault cannot be delivered (the CPU would push the exception
        // frame onto the same broken stack), so it becomes a double fault.
        if (cpu_read_cr2() >= VIRT_STACKS_START && cpu_read_cr2() < VIRT_TEST_START
            && f->rsp >= VIRT_STACKS_START && f->rsp < VIRT_TEST_START) {
            kprintf("  kernel stack overflow: guard page hit at CR2=%p (RSP=%p)\n",
                    (void *)cpu_read_cr2(), (void *)f->rsp);
        }
        break;
    default:
        kprintf("\n%s at RIP=%p, error code %lx\n", name, (void *)f->rip, f->error_code);
        break;
    }

    interrupt_dump_frame(f);
    panic("unhandled CPU exception %lu (%s) in %s mode",
          f->vector, name, (f->cs & 3) ? "user" : "kernel");
}

// Breakpoint is not an error: report it and continue after the int3.
static void breakpoint_handler(struct interrupt_frame *f) {
    // For #BP the CPU saves RIP of the instruction AFTER int3.
    kprintf("Breakpoint at RIP=%p\n", (void *)f->rip);
}

void interrupt_dispatch(struct interrupt_frame *frame) {
    interrupt_handler_t handler = handlers[frame->vector];
    if (handler != NULL) {
        handler(frame);
        return;
    }
    if (frame->vector < 32) {
        exception_fatal(frame);
    }
    kprintf("warning: unexpected interrupt %lu\n", frame->vector);
}

void interrupts_init(void) {
    idt_init();
    interrupt_register(3, breakpoint_handler);
}
