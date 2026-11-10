#pragma once

#include <stdint.h>
#include <stddef.h>
#include <limine.h>

// Everything the bootloader (Limine) tells us, collected in one place.
// boot_init() checks the responses and fills this struct; the rest of the
// kernel reads it and never touches the Limine request structs directly.

struct boot_module {
    const char *path;        // e.g. "/boot/initrd.tar"
    void *virt_addr;         // VIRTUAL address (in the HHDM) of the loaded file
    uint64_t size;           // size in bytes
};

#define BOOT_MAX_MODULES 8

struct boot_info {
    // Higher Half Direct Map: physical address P is mapped at virtual P + hhdm_offset.
    uint64_t hhdm_offset;

    // Physical memory map (list of RAM regions and what they are used for).
    struct limine_memmap_entry **memmap;
    uint64_t memmap_count;

    // Where Limine loaded the kernel (needed to build our own page tables).
    uint64_t kernel_phys_base;
    uint64_t kernel_virt_base;

    struct limine_framebuffer *framebuffer;

    // Kernel command line from limine.conf ("" if none).
    const char *cmdline;

    struct boot_module modules[BOOT_MAX_MODULES];
    uint64_t module_count;
};

extern struct boot_info boot_info;

// Check Limine's answers and fill boot_info. Panics if something required is missing.
void boot_init(void);
