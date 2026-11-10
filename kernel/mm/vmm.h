#pragma once

#include <stdint.h>
#include <stdbool.h>

// Virtual Memory Manager: x86_64 4-level paging.
//
// A 48-bit virtual address is split into 4 table indexes + page offset:
//
//   47      39 38      30 29      21 20      12 11         0
//   [ PML4 i ] [ PDPT i ] [  PD i  ] [  PT i  ] [  offset  ]
//    9 bits     9 bits     9 bits     9 bits     12 bits
//
// Each table is one 4 KiB page with 512 entries of 8 bytes. An entry holds the
// PHYSICAL address of the next table (or of the final page) plus flag bits.
// CR3 holds the PHYSICAL address of the PML4.
//
// Virtual memory layout (kernel half = PML4 entries 256-511, shared by all
// address spaces):
//   0x0000000000000000 - 0x00007fffffffffff  user space (per process, Stage 7)
//   0xffff800000000000 - ...                 HHDM: all physical RAM (from Limine's offset)
//   0xffffc00000000000 - 0xffffc0003fffffff  kernel heap (1 GiB max)
//   0xffffd00000000000 - ...                 kernel task stacks with guard pages (Stage 6)
//   0xffffe00000000000 - ...                 scratch area for tests
//   0xffffffff80000000 - ...                 kernel image

// Page table entry flags.
#define PTE_PRESENT  (1ul << 0)
#define PTE_WRITE    (1ul << 1)
#define PTE_USER     (1ul << 2)    // ring 3 may access
#define PTE_PWT      (1ul << 3)
#define PTE_PCD      (1ul << 4)    // cache disable
#define PTE_ACCESSED (1ul << 5)
#define PTE_DIRTY    (1ul << 6)
#define PTE_HUGE     (1ul << 7)    // in PDPT/PD: 1 GiB / 2 MiB page instead of a table
#define PTE_GLOBAL   (1ul << 8)
#define PTE_NX       (1ul << 63)   // no execute (needs EFER.NXE)
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ul

// Virtual memory regions.
#define VIRT_HEAP_START   0xffffc00000000000ul
#define VIRT_HEAP_MAX     (1ul << 30)
#define VIRT_STACKS_START 0xffffd00000000000ul
#define VIRT_TEST_START   0xffffe00000000000ul
#define VIRT_USER_END     0x0000800000000000ul   // first non-canonical address

// An address space = one PML4 (and all tables under it).
struct address_space {
    uint64_t phys_pml4;
};

// Build the kernel's own page tables, switch CR3 to them. Needs pmm_init().
void vmm_init(void);

// The kernel address space (also the one in use until user processes exist).
struct address_space *vmm_kernel_space(void);

// Map one 4 KiB page virt -> phys with flags (PTE_WRITE, PTE_USER, PTE_NX...).
// Returns false if out of memory. Panics if virt is already mapped.
bool vmm_map_page(struct address_space *as, uint64_t virt, uint64_t phys, uint64_t flags);

// Remove a mapping. Returns the PHYSICAL page it pointed to (0 if none).
// Does not free that page.
uint64_t vmm_unmap_page(struct address_space *as, uint64_t virt);

// PHYSICAL address virt translates to, or (uint64_t)-1 if not mapped.
uint64_t vmm_translate(struct address_space *as, uint64_t virt);

// Flags of the final page table entry for virt (0 if not mapped).
uint64_t vmm_get_flags(struct address_space *as, uint64_t virt);

// Map "count" pages at virt with fresh zeroed physical pages.
// Returns false (and undoes what it did) if memory runs out.
bool vmm_alloc_pages(struct address_space *as, uint64_t virt, uint64_t count, uint64_t flags);

// Unmap "count" pages at virt and give their physical pages back to the PMM.
void vmm_free_pages(struct address_space *as, uint64_t virt, uint64_t count);

// --- User address spaces (one per process) ---

// New address space: empty user half (PML4 entries 0-255), kernel half
// (entries 256-511) pointing to the SAME tables as the kernel space, so the
// kernel is mapped identically in every process. Returns NULL if out of memory.
struct address_space *vmm_create_user_space(void);

// Free every user page, every user page table, the PML4 and the struct.
// Must not be the address space in CR3.
void vmm_destroy_user_space(struct address_space *as);

// Is [virt, virt + len) inside user space, mapped with PTE_USER, and also
// writable if "write"? Used to check pointers that come from user programs.
bool vmm_user_range_ok(struct address_space *as, uint64_t virt, uint64_t len, bool write);

// Load an address space into CR3.
void vmm_switch(struct address_space *as);

// Number of page-table pages currently allocated (for leak tests).
uint64_t vmm_table_pages(void);
