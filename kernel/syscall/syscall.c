#include "syscall/syscall.h"

#include <stddef.h>
#include <stdbool.h>
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/idt.h"
#include "sched/task.h"
#include "mm/vmm.h"
#include "drivers/input.h"
#include "drivers/pit.h"
#include "lib/kprintf.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/gdt.h"

// MSRs for syscall/sysret.
#define MSR_STAR   0xC0000081   // segment selectors for syscall and sysret
#define MSR_LSTAR  0xC0000082   // RIP of the syscall entry point (64-bit mode)
#define MSR_FMASK  0xC0000084   // RFLAGS bits cleared on syscall
#define EFER_SCE   (1ul << 0)   // EFER: syscall/sysret enable

#define RFLAGS_TF  (1ul << 8)
#define RFLAGS_DF  (1ul << 10)
#define RFLAGS_AC  (1ul << 18)

extern void syscall_entry(void);   // syscall_entry.asm

#define SYSCALL_VECTOR 0x80

// ---------------------------------------------------------------------------
// User pointers
//
// A pointer from a user program can point anywhere: into the kernel, to an
// unmapped page, or wrap around the end of the address space. The kernel
// runs in ring 0 and could read or write ANY of that, so every user pointer
// is checked first: the whole range must be in the user half and mapped
// with the user bit (and writable, if the kernel will write to it).
// A bad pointer makes the syscall fail with -EFAULT; the kernel never touches it.
// ---------------------------------------------------------------------------

static bool user_ok(uint64_t ptr, uint64_t len, bool write) {
    return vmm_user_range_ok(task_current()->as, ptr, len, write);
}

// ---------------------------------------------------------------------------
// The syscalls
// ---------------------------------------------------------------------------

static int64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len) {
    if (fd != 1 && fd != 2) {
        return -EBADF;
    }
    if (!user_ok(buf, len, false)) {
        return -EFAULT;
    }
    const char *s = (const char *)buf;   // safe now: checked, and in our CR3
    for (uint64_t i = 0; i < len; i++) {
        kprintf("%c", s[i]);
    }
    return (int64_t)len;
}

static int64_t sys_read(uint64_t fd, uint64_t buf, uint64_t len) {
    if (fd != 0) {
        return -EBADF;
    }
    if (len == 0) {
        return 0;
    }
    if (!user_ok(buf, len, true)) {
        return -EFAULT;
    }
    char *dst = (char *)buf;
    // Wait for the first character, then take whatever else is already there.
    uint64_t n = 0;
    dst[n++] = input_getc();
    char c;
    while (n < len && input_try_getc(&c)) {
        dst[n++] = c;
    }
    return (int64_t)n;
}

static int64_t sys_exit(uint64_t code) {
    struct task *t = task_current();
    kprintf("[process '%s' (pid %lu) exited with code %ld]\n", t->name, t->id, (int64_t)code);
    task_exit((int)code);
}

int64_t syscall_dispatch(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5) {
    (void)a4;
    (void)a5;
    switch (nr) {
    case SYS_WRITE:  return sys_write(a1, a2, a3);
    case SYS_READ:   return sys_read(a1, a2, a3);
    case SYS_EXIT:   return sys_exit(a1);
    case SYS_GETPID: return (int64_t)task_current()->id;
    case SYS_SLEEP:  task_sleep_ms(a1); return 0;
    case SYS_UPTIME: return (int64_t)pit_uptime_ms();
    default:         return -ENOSYS;
    }
}

// int 0x80 entry: the arguments are in the saved registers of the frame,
// and the result goes back into the saved rax (restored by iretq).
static void syscall_int80(struct interrupt_frame *f) {
    f->rax = (uint64_t)syscall_dispatch(f->rax, f->rdi, f->rsi, f->rdx, f->r10, f->r8);
}

// "syscall" entry: syscall_entry.asm built the same frame as for int 0x80.
void syscall_from_frame(struct interrupt_frame *f);
void syscall_from_frame(struct interrupt_frame *f) {
    syscall_int80(f);
}

void syscall_init(void) {
    // Way 1: int 0x80. DPL 3: ring 3 may use "int 0x80" (other vectors give #GP).
    idt_set_gate(SYSCALL_VECTOR, 0, 3, IDT_TYPE_INTERRUPT);
    interrupt_register(SYSCALL_VECTOR, syscall_int80);

    // Way 2: the syscall instruction.
    // STAR[47:32] = kernel CS for syscall (SS = CS + 8).
    // STAR[63:48] = base for sysret: SS = base + 8, CS = base + 16 (both | 3).
    // With base 0x10: SS = 0x18 (user data), CS = 0x20 (user code). This is
    // why gdt.h puts user data before user code.
    uint64_t star = ((uint64_t)GDT_KERNEL_DATA << 48) | ((uint64_t)GDT_KERNEL_CODE << 32);
    cpu_wrmsr(MSR_STAR, star);
    cpu_wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    // Clear IF (no interrupts until we are on the kernel stack), DF (C code
    // expects it clear), TF (no single-step trap in the kernel), AC.
    cpu_wrmsr(MSR_FMASK, RFLAGS_IF | RFLAGS_DF | RFLAGS_TF | RFLAGS_AC);
    cpu_wrmsr(MSR_EFER, cpu_rdmsr(MSR_EFER) | EFER_SCE);
}
