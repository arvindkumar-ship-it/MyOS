#include "mm/pmm.h"

#include <stdbool.h>
#include "mm/memory.h"
#include "lib/string.h"
#include "lib/kprintf.h"
#include "lib/panic.h"
#include "arch/x86_64/cpu.h"

static uint8_t *virt_bitmap;      // the bitmap itself, reached through the HHDM
static uint64_t bitmap_bytes;
static uint64_t total_pages;      // number of pages the bitmap covers (from phys 0)
static uint64_t usable_pages;
static uint64_t free_pages;
static uint64_t search_hint;      // page index where the next search starts

// Bit i of the bitmap describes physical page i (address i * 4096).
static inline bool page_used(uint64_t i) {
    return virt_bitmap[i / 8] & (1u << (i % 8));
}
static inline void set_used(uint64_t i) {
    virt_bitmap[i / 8] |= (uint8_t)(1u << (i % 8));
}
static inline void set_free(uint64_t i) {
    virt_bitmap[i / 8] &= (uint8_t)~(1u << (i % 8));
}

void pmm_init(void) {
    // 1. How much RAM do we have to describe? Up to the end of the highest usable region.
    uint64_t phys_highest = 0;
    for (uint64_t i = 0; i < boot_info.memmap_count; i++) {
        struct limine_memmap_entry *e = boot_info.memmap[i];
        if (e->type == LIMINE_MEMMAP_USABLE && e->base + e->length > phys_highest) {
            phys_highest = e->base + e->length;
        }
    }
    total_pages = phys_highest / PAGE_SIZE;
    bitmap_bytes = ALIGN_UP((total_pages + 7) / 8, PAGE_SIZE);

    // 2. Find a usable region big enough to hold the bitmap itself.
    uint64_t phys_bitmap = 0;
    for (uint64_t i = 0; i < boot_info.memmap_count; i++) {
        struct limine_memmap_entry *e = boot_info.memmap[i];
        uint64_t base = ALIGN_UP(e->base, PAGE_SIZE);
        if (e->type == LIMINE_MEMMAP_USABLE && base != 0
            && e->base + e->length >= base + bitmap_bytes) {
            phys_bitmap = base;
            break;
        }
    }
    if (phys_bitmap == 0) {
        panic("pmm: no usable region for a %lu byte bitmap", bitmap_bytes);
    }
    virt_bitmap = phys_to_virt(phys_bitmap);

    // 3. Everything starts as "used"; then only USABLE regions are freed.
    // Reserved, ACPI, kernel, framebuffer and bootloader memory stay used.
    // (Bootloader-reclaimable memory still holds our boot stack and the
    // memory map itself, so we leave it alone.)
    memset(virt_bitmap, 0xFF, bitmap_bytes);
    free_pages = 0;
    for (uint64_t i = 0; i < boot_info.memmap_count; i++) {
        struct limine_memmap_entry *e = boot_info.memmap[i];
        if (e->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        // Only whole pages inside the region.
        uint64_t first = ALIGN_UP(e->base, PAGE_SIZE) / PAGE_SIZE;
        uint64_t end = ALIGN_DOWN(e->base + e->length, PAGE_SIZE) / PAGE_SIZE;
        for (uint64_t p = first; p < end; p++) {
            set_free(p);
            free_pages++;
        }
    }

    // 4. The bitmap's own pages, and page 0 (so 0 can mean "no page").
    for (uint64_t p = phys_bitmap / PAGE_SIZE; p < (phys_bitmap + bitmap_bytes) / PAGE_SIZE; p++) {
        if (!page_used(p)) {
            set_used(p);
            free_pages--;
        }
    }
    if (!page_used(0)) {
        set_used(0);
        free_pages--;
    }
    usable_pages = free_pages;
    search_hint = 0;

    kprintf("pmm: %lu MiB RAM tracked, %lu MiB free, bitmap %lu KiB at phys %p\n",
            total_pages * PAGE_SIZE / (1024 * 1024), free_pages * PAGE_SIZE / (1024 * 1024),
            bitmap_bytes / 1024, (void *)phys_bitmap);
}

// Find "count" free pages in a row, starting at the hint. Returns the first index or 0.
static uint64_t find_free_run(uint64_t count) {
    uint64_t run_start = 0, run_len = 0;
    for (uint64_t n = 0; n < total_pages; n++) {
        uint64_t i = (search_hint + n) % total_pages;
        if (i == 0) {
            run_len = 0;   // wrapped around: a run cannot cross the end of RAM
        }
        // Fast skip: 8 used pages in one byte.
        if (i % 8 == 0 && virt_bitmap[i / 8] == 0xFF && n + 8 <= total_pages) {
            run_len = 0;
            n += 7;
            continue;
        }
        if (page_used(i)) {
            run_len = 0;
            continue;
        }
        if (run_len == 0) {
            run_start = i;
        }
        run_len++;
        if (run_len == count) {
            return run_start;
        }
    }
    return 0;
}

uint64_t pmm_alloc_pages(size_t count) {
    if (count == 0) {
        return 0;
    }
    // Interrupts off: tasks (Stage 6) and IRQ handlers may allocate too.
    bool was_enabled = cpu_irq_save();
    uint64_t first = find_free_run(count);
    if (first != 0) {
        for (uint64_t p = first; p < first + count; p++) {
            set_used(p);
        }
        free_pages -= count;
        search_hint = first + count;
    }
    cpu_irq_restore(was_enabled);
    return first * PAGE_SIZE;
}

uint64_t pmm_alloc_page(void) {
    return pmm_alloc_pages(1);
}

uint64_t pmm_alloc_page_zeroed(void) {
    uint64_t phys = pmm_alloc_page();
    if (phys != 0) {
        memset(phys_to_virt(phys), 0, PAGE_SIZE);
    }
    return phys;
}

void pmm_free_pages(uint64_t phys_first, size_t count) {
    if (!IS_ALIGNED(phys_first, PAGE_SIZE)) {
        panic("pmm_free: phys %p is not page aligned", (void *)phys_first);
    }
    bool was_enabled = cpu_irq_save();
    uint64_t first = phys_first / PAGE_SIZE;
    for (uint64_t p = first; p < first + count; p++) {
        if (p == 0 || p >= total_pages) {
            panic("pmm_free: phys %p is not RAM we manage", (void *)(p * PAGE_SIZE));
        }
        if (!page_used(p)) {
            panic("pmm_free: double free of phys %p", (void *)(p * PAGE_SIZE));
        }
        set_free(p);
    }
    free_pages += count;
    if (first < search_hint) {
        search_hint = first;   // reuse low pages first
    }
    cpu_irq_restore(was_enabled);
}

void pmm_free_page(uint64_t phys_page) {
    pmm_free_pages(phys_page, 1);
}

struct pmm_stats pmm_get_stats(void) {
    struct pmm_stats s = {
        .total_pages = total_pages,
        .usable_pages = usable_pages,
        .free_pages = free_pages,
    };
    return s;
}
