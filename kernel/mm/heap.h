#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

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
    uint64_t free_blocks;    // number of free blocks (1 = no fragmentation)
    uint64_t largest_free;   // biggest free block (payload bytes)
};

struct heap_stats heap_get_stats(void);

// Walk every block and check headers, links, and that free neighbours were
// merged. Prints what is wrong and returns false on any problem.
bool heap_check(void);
