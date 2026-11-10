#include "proc/process.h"

#include "mm/memory.h"
#include "mm/vmm.h"
#include "lib/string.h"
#include "lib/kprintf.h"
#include "fs/vfs.h"

void process_copy_to(struct address_space *as, uint64_t virt, const void *src, size_t len) {
    const uint8_t *from = src;
    while (len > 0) {
        // Copy at most up to the end of the current page: the next virtual
        // page can be anywhere in physical memory.
        uint64_t in_page = PAGE_SIZE - (virt & (PAGE_SIZE - 1));
        uint64_t chunk = len < in_page ? len : in_page;
        uint64_t phys = vmm_translate(as, virt);
        memcpy(phys_to_virt(phys), from, chunk);
        virt += chunk;
        from += chunk;
        len -= chunk;
    }
}

uint64_t process_map_stack(struct address_space *as) {
    uint64_t virt_bottom = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
    if (!vmm_alloc_pages(as, virt_bottom, USER_STACK_PAGES, PTE_USER | PTE_WRITE | PTE_NX)) {
        return 0;
    }
    return USER_STACK_TOP;
}

bool process_open_console(struct task *t) {
    int64_t err;
    struct file *console = vfs_open("/dev/console", O_RDWR, &err);
    if (console == NULL) {
        return false;
    }
    // fd 0 (stdin), 1 (stdout), 2 (stderr) all share one open file.
    t->fds[0] = console;
    vfs_file_ref(console);
    t->fds[1] = console;
    vfs_file_ref(console);
    t->fds[2] = console;
    return true;
}

struct task *process_spawn_flat(const char *name, const void *image, size_t size) {
    struct address_space *as = vmm_create_user_space();
    if (as == NULL) {
        return NULL;
    }
    // A flat binary mixes code and data, so its pages are writable AND
    // executable. (The ELF loader in Stage 9 gives each part its own rights.)
    uint64_t pages = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;
    if (!vmm_alloc_pages(as, USER_CODE_BASE, pages, PTE_USER | PTE_WRITE)) {
        vmm_destroy_user_space(as);
        return NULL;
    }
    process_copy_to(as, USER_CODE_BASE, image, size);

    uint64_t user_rsp = process_map_stack(as);
    if (user_rsp == 0) {
        vmm_destroy_user_space(as);
        return NULL;
    }
    struct task *t = task_create_user(name, as, USER_CODE_BASE, user_rsp);
    if (t == NULL) {
        vmm_destroy_user_space(as);
        return NULL;
    }
    if (!process_open_console(t)) {
        kprintf("process: cannot open /dev/console for '%s'\n", name);
    }
    task_start(t);
    kprintf("[process '%s' started: pid %lu, %lu bytes at %p]\n",
            name, t->id, size, (void *)USER_CODE_BASE);
    return t;
}
