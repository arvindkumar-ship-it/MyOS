#include "proc/elf.h"

#include "mm/memory.h"
#include "mm/pmm.h"
#include "proc/process.h"
#include "lib/string.h"
#include "mm/heap.h"

uint64_t elf_load(struct address_space *as, const void *file, size_t size, const char **error) {
    const struct elf64_header *h = file;
    if (size < sizeof(*h) || memcmp(h->ident, ELF_MAGIC, 4) != 0) {
        *error = "not an ELF file";
        return 0;
    }
    if (h->ident[4] != 2 || h->ident[5] != 1) {
        *error = "not a 64-bit little-endian ELF";
        return 0;
    }
    if (h->type != ELF_TYPE_EXEC || h->machine != ELF_MACHINE_X86_64) {
        *error = "not an x86_64 executable";
        return 0;
    }
    if (h->phentsize != sizeof(struct elf64_phdr)
        || h->phoff + (uint64_t)h->phnum * sizeof(struct elf64_phdr) > size) {
        *error = "bad program header table";
        return 0;
    }

    bool entry_ok = false;
    const struct elf64_phdr *ph = (const struct elf64_phdr *)((const uint8_t *)file + h->phoff);
    for (uint16_t i = 0; i < h->phnum; i++) {
        const struct elf64_phdr *p = &ph[i];
        if (p->type != PT_LOAD || p->memsz == 0) {
            continue;
        }
        // Everything must stay inside user space and inside the file.
        uint64_t end = p->vaddr + p->memsz;
        if (end < p->vaddr || p->vaddr < PAGE_SIZE || end > VIRT_USER_END
            || p->filesz > p->memsz || p->offset + p->filesz > size
            || p->offset + p->filesz < p->offset) {
            *error = "segment outside user space or outside the file";
            return 0;
        }

        // Page rights from the segment flags: user, +write if W, no-execute unless X.
        uint64_t flags = PTE_USER;
        if (p->flags & PF_W) {
            flags |= PTE_WRITE;
        }
        if (!(p->flags & PF_X)) {
            flags |= PTE_NX;
        }

        for (uint64_t virt = ALIGN_DOWN(p->vaddr, PAGE_SIZE); virt < end; virt += PAGE_SIZE) {
            if (vmm_translate(as, virt) != (uint64_t)-1) {
                *error = "two segments share a page";
                return 0;
            }
            uint64_t phys = pmm_alloc_page_zeroed();   // zeroed: this is the .bss part too
            if (phys == 0 || !vmm_map_page(as, virt, phys, flags)) {
                if (phys != 0) {
                    pmm_free_page(phys);
                }
                *error = "out of memory";
                return 0;
            }
        }
        process_copy_to(as, p->vaddr, (const uint8_t *)file + p->offset, p->filesz);

        if ((p->flags & PF_X) && h->entry >= p->vaddr && h->entry < end) {
            entry_ok = true;
        }
    }
    if (!entry_ok) {
        *error = "entry point is not in an executable segment";
        return 0;
    }
    return h->entry;
}

uint64_t elf_setup_stack(struct address_space *as, uint64_t stack_top,
                         int argc, const char *const *argv) {
    // Build the stack contents in a kernel buffer first (one page is plenty
    // for our small programs), then copy it to the top of the user stack.
    uint8_t *page = kzalloc(PAGE_SIZE);
    if (page == NULL) {
        return 0;
    }
    uint64_t used = 0;   // bytes used, counted from the top

    // 1. The strings, at the very top. Remember their USER addresses.
    uint64_t user_argv[ELF_MAX_ARGS];
    if (argc > ELF_MAX_ARGS) {
        argc = ELF_MAX_ARGS;
    }
    for (int i = argc - 1; i >= 0; i--) {
        size_t len = strlen(argv[i]) + 1;
        if (used + len > PAGE_SIZE / 2) {
            kfree(page);
            return 0;
        }
        used += len;
        memcpy(page + PAGE_SIZE - used, argv[i], len);
        user_argv[i] = stack_top - used;
    }

    // 2. The pointer part: argc, argv[], NULL, envp NULL, auxv (AT_NULL, 0).
    //    Count its size, then place it so that rsp (pointing at argc) ends
    //    up 16-byte aligned, as the ABI requires at _start.
    uint64_t words = 1 + (uint64_t)argc + 1 + 1 + 2;
    uint64_t rsp = ALIGN_DOWN(stack_top - used - words * 8, 16);
    uint64_t *w = (uint64_t *)(page + PAGE_SIZE - (stack_top - rsp));
    *w++ = (uint64_t)argc;
    for (int i = 0; i < argc; i++) {
        *w++ = user_argv[i];
    }
    *w++ = 0;   // end of argv
    *w++ = 0;   // envp: empty
    *w++ = 0;   // auxv: AT_NULL
    *w++ = 0;

    uint64_t total = stack_top - rsp;
    process_copy_to(as, rsp, page + PAGE_SIZE - total, total);
    kfree(page);
    return rsp;
}
