#pragma once

#include <stdint.h>
#include "boot.h"

// Basic memory definitions shared by PMM, VMM and heap.
//
// Two kinds of addresses — never mix them up:
//   PHYSICAL: where a byte really is in RAM. Page tables and CR3 hold these.
//             The CPU cannot use them directly once paging is on.
//   VIRTUAL:  what code uses (pointers). The page tables translate it to physical.
// Names: phys_xxx for physical addresses, virt_xxx for virtual ones.

#define PAGE_SIZE  4096ul
#define PAGE_SHIFT 12

#define ALIGN_DOWN(x, a) ((x) & ~((uint64_t)(a) - 1))
#define ALIGN_UP(x, a)   (((x) + (uint64_t)(a) - 1) & ~((uint64_t)(a) - 1))
#define IS_ALIGNED(x, a) (((x) & ((uint64_t)(a) - 1)) == 0)

// The HHDM (higher half direct map) maps ALL physical RAM at virtual
// address phys + hhdm_offset. It is how the kernel reaches a physical page
// (for example a fresh page table) through a normal pointer.
static inline void *phys_to_virt(uint64_t phys) {
    return (void *)(phys + boot_info.hhdm_offset);
}

// Only valid for addresses inside the HHDM (not for the kernel image or heap).
static inline uint64_t hhdm_virt_to_phys(const void *virt) {
    return (uint64_t)virt - boot_info.hhdm_offset;
}
