#include "mm/vmm.h"

#include <stddef.h>
#include "mm/memory.h"
#include "mm/pmm.h"
#include "lib/string.h"
#include "lib/kprintf.h"
#include "lib/panic.h"
#include "arch/x86_64/cpu.h"

#define ENTRIES_PER_TABLE 512

// Index into each level for a virtual address.
#define PML4_INDEX(v) (((v) >> 39) & 0x1FF)
#define PDPT_INDEX(v) (((v) >> 30) & 0x1FF)
#define PD_INDEX(v)   (((v) >> 21) & 0x1FF)
#define PT_INDEX(v)   (((v) >> 12) & 0x1FF)

// First PML4 entry of the kernel half (0xffff800000000000).
#define KERNEL_PML4_FIRST 256

static struct address_space kernel_space;
static uint64_t table_pages;   // page-table pages allocated by us

// Section boundaries from linker.ld (VIRTUAL addresses).
extern char __kernel_requests_start[], __kernel_requests_end[];
extern char __kernel_text_start[], __kernel_text_end[];
extern char __kernel_rodata_start[], __kernel_rodata_end[];
extern char __kernel_data_start[], __kernel_data_end[];

// A canonical address has bits 63-48 all equal to bit 47.
static bool is_canonical(uint64_t virt) {
    uint64_t top = virt >> 47;
    return top == 0 || top == 0x1FFFF;
}

static uint64_t *table_virt(uint64_t phys_table) {
    return phys_to_virt(phys_table);
}

// Follow entry "index" of "table" to the next level table.
// If it is missing and create is true, allocate a zeroed table.
// Returns the next table (VIRTUAL pointer through the HHDM) or NULL.
static uint64_t *next_table(uint64_t *table, uint64_t index, bool create, bool user) {
    uint64_t entry = table[index];
    if (entry & PTE_PRESENT) {
        if (entry & PTE_HUGE) {
            panic("vmm: huge page in the way (entry %lx)", entry);
        }
        // A user mapping below a kernel-only table would be unreachable for
        // ring 3: every level must allow user access.
        if (user && !(entry & PTE_USER)) {
            table[index] = entry | PTE_USER;
        }
        return table_virt(entry & PTE_ADDR_MASK);
    }
    if (!create) {
        return NULL;
    }
    uint64_t phys = pmm_alloc_page_zeroed();
    if (phys == 0) {
        return NULL;
    }
    table_pages++;
    // Upper levels are permissive (present + write, + user if needed);
    // the final page table entry decides the real permissions.
    table[index] = phys | PTE_PRESENT | PTE_WRITE | (user ? PTE_USER : 0);
    return table_virt(phys);
}

// Pointer to the final page table entry for virt (or NULL if a table is missing).
static uint64_t *walk(struct address_space *as, uint64_t virt, bool create, bool user) {
    uint64_t *pml4 = table_virt(as->phys_pml4);
    uint64_t *pdpt = next_table(pml4, PML4_INDEX(virt), create, user);
    if (pdpt == NULL) {
        return NULL;
    }
    uint64_t *pd = next_table(pdpt, PDPT_INDEX(virt), create, user);
    if (pd == NULL) {
        return NULL;
    }
    uint64_t *pt = next_table(pd, PD_INDEX(virt), create, user);
    if (pt == NULL) {
        return NULL;
    }
    return &pt[PT_INDEX(virt)];
}

static bool is_current(struct address_space *as) {
    return (cpu_read_cr3() & PTE_ADDR_MASK) == as->phys_pml4;
}

bool vmm_map_page(struct address_space *as, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!IS_ALIGNED(virt, PAGE_SIZE) || !IS_ALIGNED(phys, PAGE_SIZE) || !is_canonical(virt)) {
        panic("vmm_map_page: bad address virt %p phys %p", (void *)virt, (void *)phys);
    }
    bool was_enabled = cpu_irq_save();
    uint64_t *pte = walk(as, virt, true, (flags & PTE_USER) != 0);
    if (pte == NULL) {
        cpu_irq_restore(was_enabled);
        return false;
    }
    if (*pte & PTE_PRESENT) {
        panic("vmm_map_page: virt %p is already mapped (to phys %p)",
              (void *)virt, (void *)(*pte & PTE_ADDR_MASK));
    }
    *pte = phys | flags | PTE_PRESENT;
    // A not-present entry is never cached in the TLB, so no invlpg needed here.
    cpu_irq_restore(was_enabled);
    return true;
}

uint64_t vmm_unmap_page(struct address_space *as, uint64_t virt) {
    bool was_enabled = cpu_irq_save();
    uint64_t *pte = walk(as, virt, false, false);
    uint64_t phys = 0;
    if (pte != NULL && (*pte & PTE_PRESENT)) {
        phys = *pte & PTE_ADDR_MASK;
        *pte = 0;
        // The CPU may still have the old translation cached in its TLB.
        if (is_current(as)) {
            cpu_invlpg(virt);
        }
    }
    cpu_irq_restore(was_enabled);
    return phys;
}

uint64_t vmm_translate(struct address_space *as, uint64_t virt) {
    uint64_t *pte = walk(as, ALIGN_DOWN(virt, PAGE_SIZE), false, false);
    if (pte == NULL || !(*pte & PTE_PRESENT)) {
        return (uint64_t)-1;
    }
    return (*pte & PTE_ADDR_MASK) | (virt & (PAGE_SIZE - 1));
}

uint64_t vmm_get_flags(struct address_space *as, uint64_t virt) {
    uint64_t *pte = walk(as, ALIGN_DOWN(virt, PAGE_SIZE), false, false);
    if (pte == NULL || !(*pte & PTE_PRESENT)) {
        return 0;
    }
    return *pte & ~PTE_ADDR_MASK;
}

void vmm_free_pages(struct address_space *as, uint64_t virt, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        uint64_t phys = vmm_unmap_page(as, virt + i * PAGE_SIZE);
        if (phys != 0) {
            pmm_free_page(phys);
        }
    }
}

bool vmm_alloc_pages(struct address_space *as, uint64_t virt, uint64_t count, uint64_t flags) {
    for (uint64_t i = 0; i < count; i++) {
        uint64_t phys = pmm_alloc_page_zeroed();
        if (phys == 0 || !vmm_map_page(as, virt + i * PAGE_SIZE, phys, flags)) {
            if (phys != 0) {
                pmm_free_page(phys);
            }
            vmm_free_pages(as, virt, i);   // undo the pages mapped so far
            return false;
        }
    }
    return true;
}

void vmm_switch(struct address_space *as) {
    cpu_write_cr3(as->phys_pml4);
}

struct address_space *vmm_kernel_space(void) {
    return &kernel_space;
}

uint64_t vmm_table_pages(void) {
    return table_pages;
}

// Map a range of the kernel image: VIRTUAL [virt_start, virt_end) to where
// Limine put it in PHYSICAL memory.
static void map_kernel_range(const char *virt_start, const char *virt_end, uint64_t flags) {
    uint64_t start = ALIGN_DOWN((uint64_t)virt_start, PAGE_SIZE);
    uint64_t end = ALIGN_UP((uint64_t)virt_end, PAGE_SIZE);
    for (uint64_t virt = start; virt < end; virt += PAGE_SIZE) {
        uint64_t phys = virt - boot_info.kernel_virt_base + boot_info.kernel_phys_base;
        if (!vmm_map_page(&kernel_space, virt, phys, flags)) {
            panic("vmm: out of memory mapping the kernel");
        }
    }
}

void vmm_init(void) {
    // NX bit only works when EFER.NXE is on (Limine normally sets it already).
    uint64_t efer = cpu_rdmsr(MSR_EFER);
    if (!(efer & EFER_NXE)) {
        cpu_wrmsr(MSR_EFER, efer | EFER_NXE);
    }
    // WP: the kernel itself must not be able to write to read-only pages.
    cpu_write_cr0(cpu_read_cr0() | CR0_WP);

    kernel_space.phys_pml4 = pmm_alloc_page_zeroed();
    if (kernel_space.phys_pml4 == 0) {
        panic("vmm: no page for the PML4");
    }
    table_pages = 1;

    // Create all 256 kernel-half PDPTs now. User address spaces (Stage 7) copy
    // these PML4 entries, so any kernel mapping added later (heap, stacks)
    // shows up in every address space automatically.
    uint64_t *pml4 = table_virt(kernel_space.phys_pml4);
    for (int i = KERNEL_PML4_FIRST; i < ENTRIES_PER_TABLE; i++) {
        if (next_table(pml4, (uint64_t)i, true, false) == NULL) {
            panic("vmm: out of memory for kernel PDPTs");
        }
    }

    // 1. The kernel image, each part with its own permissions.
    map_kernel_range(__kernel_requests_start, __kernel_requests_end, PTE_WRITE | PTE_NX);
    map_kernel_range(__kernel_text_start, __kernel_text_end, 0);                 // read + execute
    map_kernel_range(__kernel_rodata_start, __kernel_rodata_end, PTE_NX);        // read only
    map_kernel_range(__kernel_data_start, __kernel_data_end, PTE_WRITE | PTE_NX);

    // 2. The HHDM: physical RAM at phys + hhdm_offset, read/write, never executable.
    // "Reserved" regions are skipped (one of them is 12 GiB of nothing in QEMU).
    uint64_t hhdm_pages = 0;
    for (uint64_t i = 0; i < boot_info.memmap_count; i++) {
        struct limine_memmap_entry *e = boot_info.memmap[i];
        switch (e->type) {
        case LIMINE_MEMMAP_USABLE:
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
        case LIMINE_MEMMAP_ACPI_NVS:
        case LIMINE_MEMMAP_FRAMEBUFFER:
            break;
        default:
            continue;
        }
        uint64_t start = ALIGN_DOWN(e->base, PAGE_SIZE);
        uint64_t end = ALIGN_UP(e->base + e->length, PAGE_SIZE);
        for (uint64_t phys = start; phys < end; phys += PAGE_SIZE) {
            uint64_t virt = phys + boot_info.hhdm_offset;
            // Regions can share a page at their edges; map it once.
            if (vmm_translate(&kernel_space, virt) != (uint64_t)-1) {
                continue;
            }
            if (!vmm_map_page(&kernel_space, virt, phys, PTE_WRITE | PTE_NX)) {
                panic("vmm: out of memory mapping the HHDM");
            }
            hhdm_pages++;
        }
    }

    // 3. Switch. From now on the CPU uses only our tables; Limine's are unused.
    uint64_t phys_old_cr3 = cpu_read_cr3();
    vmm_switch(&kernel_space);

    kprintf("vmm: own page tables active (CR3 %p -> %p), HHDM %lu MiB, %lu table pages\n",
            (void *)phys_old_cr3, (void *)kernel_space.phys_pml4,
            hhdm_pages * PAGE_SIZE / (1024 * 1024), table_pages);
}
