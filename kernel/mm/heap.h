#pragma once

#include <stddef.h>
#include <stdint.h>

// Kernel heap: kmalloc/kfree for small objects of any size.
// Lives at VIRT_HEAP_START; pages are mapped from the PMM as the heap grows.

void heap_init(void);

// Returns 16-byte aligned memory, or NULL if out of memory (or size == 0).
void *kmalloc(size_t size);

// Same, filled with zeros.
void *kzalloc(size_t size);

void kfree(void *ptr);

struct heap_stats {
    uint64_t mapped_bytes;   // heap pages mapped so far
    uint64_t used_bytes;     // bytes handed out (payload, without headers)
    uint64_t allocations;    // live allocations
};

struct heap_stats heap_get_stats(void);
