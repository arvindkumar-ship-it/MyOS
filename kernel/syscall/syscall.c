#include "syscall/syscall.h"

#include <stddef.h>
#include <stdbool.h>
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/idt.h"
#include "sched/task.h"
#include "mm/vmm.h"
#include "drivers/pit.h"
#include "fs/vfs.h"
#include "proc/process.h"
#include "proc/elf.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "mm/memory.h"
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

// Copy a '\0'-terminated string from user memory into buf (at most size
// bytes with the '\0'). Checks every byte's page before reading it.
// Returns the length, -EFAULT, or -ENAMETOOLONG.
static int64_t copy_user_string(uint64_t user_ptr, char *buf, uint64_t size) {
    for (uint64_t i = 0; i < size; i++) {
        uint64_t addr = user_ptr + i;
        // Check once per page (and for the very first byte).
        if (i == 0 || (addr & (PAGE_SIZE - 1)) == 0) {
            if (!user_ok(addr, 1, false)) {
                return -EFAULT;
            }
        }
        buf[i] = *(const char *)addr;
        if (buf[i] == '\0') {
            return (int64_t)i;
        }
    }
    return -ENAMETOOLONG;
}

// The open file behind fd, or NULL.
static struct file *fd_get(uint64_t fd) {
    if (fd >= TASK_MAX_FDS) {
        return NULL;
    }
    return task_current()->fds[fd];
}

static int64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len) {
    struct file *f = fd_get(fd);
    if (f == NULL) {
        return -EBADF;
    }
    if (!user_ok(buf, len, false)) {
        return -EFAULT;
    }
    // Safe now: the range is checked, and it is in the current CR3.
    return vfs_write(f, (const void *)buf, len);
}

static int64_t sys_read(uint64_t fd, uint64_t buf, uint64_t len) {
    struct file *f = fd_get(fd);
    if (f == NULL) {
        return -EBADF;
    }
    if (!user_ok(buf, len, true)) {
        return -EFAULT;
    }
    return vfs_read(f, (void *)buf, len);
}

static int64_t sys_open(uint64_t user_path, uint64_t flags) {
    char path[256];
    int64_t len = copy_user_string(user_path, path, sizeof(path));
    if (len < 0) {
        return len;
    }
    struct task *t = task_current();
    int fd = -1;
    for (int i = 0; i < TASK_MAX_FDS; i++) {
        if (t->fds[i] == NULL) {
            fd = i;
            break;
        }
    }
    if (fd < 0) {
        return -EMFILE;
    }
    int64_t err;
    struct file *f = vfs_open(path, (int)flags, &err);
    if (f == NULL) {
        return err;
    }
    t->fds[fd] = f;
    return fd;
}

static int64_t sys_close(uint64_t fd) {
    struct file *f = fd_get(fd);
    if (f == NULL) {
        return -EBADF;
    }
    task_current()->fds[fd] = NULL;
    vfs_close(f);
    return 0;
}

// spawn(path, argv): argv is a user array of user string pointers, ended by NULL.
// Everything is copied into one kernel buffer before the new process is built.
#define SPAWN_STRINGS 2048

static int64_t sys_spawn(uint64_t user_path, uint64_t user_argv) {
    char path[256];
    int64_t len = copy_user_string(user_path, path, sizeof(path));
    if (len < 0) {
        return len;
    }
    char *strings = kmalloc(SPAWN_STRINGS);
    if (strings == NULL) {
        return -ENOMEM;
    }
    const char *argv[ELF_MAX_ARGS];
    int argc = 0;
    uint64_t used = 0;
    int64_t result = 0;
    for (;; argc++) {
        uint64_t slot = user_argv + (uint64_t)argc * 8;
        if (!user_ok(slot, 8, false)) {
            result = -EFAULT;
            break;
        }
        uint64_t user_str = *(const uint64_t *)slot;
        if (user_str == 0) {
            break;   // end of argv
        }
        if (argc == ELF_MAX_ARGS) {
            result = -E2BIG;
            break;
        }
        int64_t n = copy_user_string(user_str, strings + used, SPAWN_STRINGS - used);
        if (n < 0) {
            result = n == -ENAMETOOLONG ? -E2BIG : n;
            break;
        }
        argv[argc] = strings + used;
        used += (uint64_t)n + 1;
    }
    if (result == 0) {
        const char *error;
        struct task *child = process_spawn_elf(path, argc, argv, task_current(), &error);
        if (child == NULL) {
            result = vfs_lookup(path) == NULL ? -ENOENT : -ENOEXEC;
        } else {
            result = (int64_t)child->id;
        }
    }
    kfree(strings);
    return result;
}

static int64_t sys_wait(uint64_t pid, uint64_t user_status) {
    if (user_status != 0 && !user_ok(user_status, sizeof(int), true)) {
        return -EFAULT;
    }
    int code = 0;
    int64_t r = task_wait(pid, &code);
    if (r < 0) {
        return -ECHILD;
    }
    if (user_status != 0) {
        *(int *)user_status = code;
    }
    return r;
}

static void fill_type_size(struct vfs_node *n, uint32_t *type, uint64_t *size) {
    *type = n->type == VFS_DIR ? MYOS_TYPE_DIR : n->type == VFS_DEV ? MYOS_TYPE_DEV : MYOS_TYPE_FILE;
    *size = n->size;
}

static int64_t sys_readdir(uint64_t user_path, uint64_t index, uint64_t user_out) {
    char path[256];
    int64_t len = copy_user_string(user_path, path, sizeof(path));
    if (len < 0) {
        return len;
    }
    if (!user_ok(user_out, sizeof(struct myos_dirent), true)) {
        return -EFAULT;
    }
    struct vfs_node *dir = vfs_lookup(path);
    if (dir == NULL) {
        return -ENOENT;
    }
    if (dir->type != VFS_DIR) {
        return -ENOTDIR;
    }
    struct vfs_node *n = vfs_readdir(dir, index);
    if (n == NULL) {
        return 0;
    }
    struct myos_dirent *out = (struct myos_dirent *)user_out;
    memset(out, 0, sizeof(*out));
    memcpy(out->name, n->name, VFS_NAME_MAX < MYOS_NAME_MAX ? VFS_NAME_MAX : MYOS_NAME_MAX);
    out->name[MYOS_NAME_MAX - 1] = '\0';
    fill_type_size(n, &out->type, &out->size);
    return 1;
}

static int64_t sys_stat(uint64_t user_path, uint64_t user_out) {
    char path[256];
    int64_t len = copy_user_string(user_path, path, sizeof(path));
    if (len < 0) {
        return len;
    }
    if (!user_ok(user_out, sizeof(struct myos_stat), true)) {
        return -EFAULT;
    }
    struct vfs_node *n = vfs_lookup(path);
    if (n == NULL) {
        return -ENOENT;
    }
    struct myos_stat *out = (struct myos_stat *)user_out;
    memset(out, 0, sizeof(*out));
    fill_type_size(n, &out->type, &out->size);
    return 0;
}

// ps: copy a snapshot of the task list. Interrupts are off while
// task_for_each runs, so we fill a kernel buffer first and copy it out after.
struct ps_ctx {
    struct myos_procinfo *buf;
    uint64_t max;
    uint64_t count;
};

static void ps_collect(struct task *t, void *arg) {
    struct ps_ctx *ctx = arg;
    if (ctx->count == ctx->max) {
        return;
    }
    struct myos_procinfo *p = &ctx->buf[ctx->count++];
    memset(p, 0, sizeof(*p));
    p->pid = t->id;
    p->ppid = t->parent != NULL ? t->parent->id : 0;
    p->ticks = t->ticks_used;
    p->is_user = t->is_user;
    const char *state = task_state_name(t->state);
    for (int i = 0; i < 11 && state[i]; i++) {
        p->state[i] = state[i];
    }
    memcpy(p->name, t->name, TASK_NAME_LEN < 16 ? TASK_NAME_LEN : 16);
    p->name[15] = '\0';
}

static int64_t sys_ps(uint64_t user_buf, uint64_t max) {
    if (max > 64) {
        max = 64;
    }
    if (!user_ok(user_buf, max * sizeof(struct myos_procinfo), true)) {
        return -EFAULT;
    }
    struct ps_ctx ctx = { .buf = kmalloc(max * sizeof(struct myos_procinfo) + 1), .max = max };
    if (ctx.buf == NULL) {
        return -ENOMEM;
    }
    task_for_each(ps_collect, &ctx);
    memcpy((void *)user_buf, ctx.buf, ctx.count * sizeof(struct myos_procinfo));
    kfree(ctx.buf);
    return (int64_t)ctx.count;
}

static int64_t sys_exit(uint64_t code) {
    struct task *t = task_current();
    // Processes started by the kernel are announced; a shell's children are quiet.
    if (t->parent == NULL) {
        kprintf("[process '%s' (pid %lu) exited with code %ld]\n", t->name, t->id, (int64_t)code);
    }
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
    case SYS_OPEN:   return sys_open(a1, a2);
    case SYS_CLOSE:  return sys_close(a1);
    case SYS_SPAWN:  return sys_spawn(a1, a2);
    case SYS_WAIT:   return sys_wait(a1, a2);
    case SYS_READDIR: return sys_readdir(a1, a2, a3);
    case SYS_STAT:   return sys_stat(a1, a2);
    case SYS_PS:     return sys_ps(a1, a2);
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
