#pragma once

#include <stdint.h>
#include <stddef.h>

// Physical Memory Manager: hands out 4 KiB physical pages ("frames").
// It only knows PHYSICAL addresses; to touch a page, use phys_to_virt().
//
// Bookkeeping: a bitmap with one bit per page of RAM (1 = used, 0 = free),
// built from the Limine memory map.

// Build the bitmap. Needs boot_init() (memory map, HHDM).
void pmm_init(void);

// Allocate one page. Returns its PHYSICAL address, or 0 if RAM is full.
// (Physical page 0 is never handed out, so 0 can mean "failed".)
uint64_t pmm_alloc_page(void);

// Same, filled with zeros.
uint64_t pmm_alloc_page_zeroed(void);

// "count" physically contiguous pages. Returns the first one's PHYSICAL address, or 0.
uint64_t pmm_alloc_pages(size_t count);

// Give pages back. Panics on a double free or an address we never handed out.
void pmm_free_page(uint64_t phys_page);
void pmm_free_pages(uint64_t phys_first, size_t count);

struct pmm_stats {
    uint64_t total_pages;   // pages covered by the bitmap
    uint64_t usable_pages;  // pages that were free at boot (after the bitmap itself)
    uint64_t free_pages;    // free right now
};

struct pmm_stats pmm_get_stats(void);
