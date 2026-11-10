#include "mm/heap.h"

#include <stdbool.h>
#include "mm/memory.h"
#include "mm/vmm.h"
#include "lib/string.h"
#include "lib/kprintf.h"
#include "lib/panic.h"
#include "arch/x86_64/cpu.h"

// First version: a bump allocator.
// "next" only moves forward. kmalloc hands out [next, next + size) and moves
// next; kfree does nothing, so memory is never reused. Very simple, and fine
// for things allocated once at boot, but it leaks everything else.

#define ALIGNMENT 16

static uint64_t virt_next;        // next free byte
static uint64_t virt_mapped_end;  // end of the mapped heap pages
static uint64_t allocations;

void heap_init(void) {
    virt_next = VIRT_HEAP_START;
    virt_mapped_end = VIRT_HEAP_START;
    kprintf("heap: bump allocator at %p\n", (void *)VIRT_HEAP_START);
}

// Map more pages so the heap reaches at least virt_end.
static bool heap_grow(uint64_t virt_end) {
    uint64_t new_end = ALIGN_UP(virt_end, PAGE_SIZE);
    if (new_end > VIRT_HEAP_START + VIRT_HEAP_MAX) {
        return false;
    }
    uint64_t pages = (new_end - virt_mapped_end) / PAGE_SIZE;
    if (!vmm_alloc_pages(vmm_kernel_space(), virt_mapped_end, pages, PTE_WRITE | PTE_NX)) {
        return false;
    }
    virt_mapped_end = new_end;
    return true;
}

void *kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    bool was_enabled = cpu_irq_save();
    uint64_t start = ALIGN_UP(virt_next, ALIGNMENT);
    uint64_t end = start + size;
    void *result = NULL;
    if (end <= virt_mapped_end || heap_grow(end)) {
        virt_next = end;
        allocations++;
        result = (void *)start;
    }
    cpu_irq_restore(was_enabled);
    return result;
}

void *kzalloc(size_t size) {
    void *p = kmalloc(size);
    if (p != NULL) {
        memset(p, 0, size);
    }
    return p;
}

void kfree(void *ptr) {
    (void)ptr;   // bump allocator: memory is never given back
}

struct heap_stats heap_get_stats(void) {
    struct heap_stats s = {
        .mapped_bytes = virt_mapped_end - VIRT_HEAP_START,
        .used_bytes = virt_next - VIRT_HEAP_START,
        .allocations = allocations,
    };
    return s;
}
