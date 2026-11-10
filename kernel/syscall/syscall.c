#include "syscall/syscall.h"

#include <stddef.h>
#include <stdbool.h>
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/idt.h"
#include "sched/task.h"
#include "mm/vmm.h"
#include "drivers/input.h"
#include "lib/kprintf.h"

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
    default:         return -ENOSYS;
    }
}

// int 0x80 entry: the arguments are in the saved registers of the frame,
// and the result goes back into the saved rax (restored by iretq).
static void syscall_int80(struct interrupt_frame *f) {
    f->rax = (uint64_t)syscall_dispatch(f->rax, f->rdi, f->rsi, f->rdx, f->r10, f->r8);
}

void syscall_init(void) {
    // DPL 3: ring 3 may use "int 0x80" (for other vectors it would get #GP).
    idt_set_gate(SYSCALL_VECTOR, 0, 3, IDT_TYPE_INTERRUPT);
    interrupt_register(SYSCALL_VECTOR, syscall_int80);
}
