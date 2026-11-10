#include "mm/heap.h"

#include <stdbool.h>
#include "mm/memory.h"
#include "mm/vmm.h"
#include "lib/string.h"
#include "lib/kprintf.h"
#include "lib/panic.h"
#include "arch/x86_64/cpu.h"

// Free-list allocator.
//
// The heap is one long run of blocks, back to back, in address order:
//
//   [hdr|payload....][hdr|payload..][hdr|payload..........]
//
// Every block (used or free) starts with a header. The headers form a doubly
// linked list in address order, so a block can find its neighbours.
//   kmalloc: first fit - take the first free block that is big enough;
//            split it if the rest is big enough to be a block of its own.
//   kfree:   mark the block free and merge ("coalesce") it with a free
//            neighbour on either side, so free space does not stay chopped up.
// When no block fits, the heap grows: more pages are mapped at its end.

#define ALIGNMENT     16
#define MIN_PAYLOAD   16
#define MAGIC_USED    0xA110C8EDu
#define MAGIC_FREE    0xF4EEB10Cu
#define POISON_FREED  0xDD   // freed memory is filled with this (catches use-after-free)

struct block {
    uint32_t magic;          // MAGIC_USED or MAGIC_FREE; anything else = corruption
    uint32_t reserved;
    uint64_t size;           // payload bytes (multiple of 16), header not included
    struct block *prev;      // neighbour at a lower address (NULL for the first)
    struct block *next;      // neighbour at a higher address (NULL for the last)
};

// 32 bytes: payloads stay 16-byte aligned.
_Static_assert(sizeof(struct block) % ALIGNMENT == 0, "header size must keep alignment");
#define HEADER sizeof(struct block)

static struct block *first_block;
static struct block *last_block;
static uint64_t virt_mapped_end;
static uint64_t used_bytes;
static uint64_t allocations;

static inline void *payload(struct block *b) {
    return (uint8_t *)b + HEADER;
}

static inline uint8_t *block_end(struct block *b) {
    return (uint8_t *)payload(b) + b->size;
}

void heap_init(void) {
    first_block = NULL;
    last_block = NULL;
    virt_mapped_end = VIRT_HEAP_START;
    used_bytes = 0;
    allocations = 0;
    kprintf("heap: free-list allocator at %p (max %lu MiB)\n",
            (void *)VIRT_HEAP_START, VIRT_HEAP_MAX / (1024 * 1024));
}

// Make the heap at least "bytes" bigger. The new space becomes free space
// at the end (merged into the last block if that one is free).
static bool heap_grow(uint64_t bytes) {
    uint64_t grow = ALIGN_UP(bytes, PAGE_SIZE);
    if (virt_mapped_end + grow > VIRT_HEAP_START + VIRT_HEAP_MAX) {
        return false;
    }
    if (!vmm_alloc_pages(vmm_kernel_space(), virt_mapped_end, grow / PAGE_SIZE,
                         PTE_WRITE | PTE_NX)) {
        return false;
    }
    uint64_t virt_new = virt_mapped_end;
    virt_mapped_end += grow;

    if (last_block != NULL && last_block->magic == MAGIC_FREE) {
        last_block->size += grow;   // just make the free tail longer
        return true;
    }
    struct block *b = (struct block *)virt_new;
    b->magic = MAGIC_FREE;
    b->size = grow - HEADER;
    b->prev = last_block;
    b->next = NULL;
    if (last_block != NULL) {
        last_block->next = b;
    } else {
        first_block = b;
    }
    last_block = b;
    return true;
}

static void check_block(struct block *b, const char *where) {
    if (b->magic != MAGIC_USED && b->magic != MAGIC_FREE) {
        panic("heap corruption in %s: block %p has magic %x", where, (void *)b, b->magic);
    }
}

// Cut block b so it has exactly "size" payload bytes; the rest becomes a new free block.
static void split(struct block *b, uint64_t size) {
    if (b->size < size + HEADER + MIN_PAYLOAD) {
        return;   // the rest would be too small to be useful: keep it inside b
    }
    struct block *rest = (struct block *)((uint8_t *)payload(b) + size);
    rest->magic = MAGIC_FREE;
    rest->size = b->size - size - HEADER;
    rest->prev = b;
    rest->next = b->next;
    if (b->next != NULL) {
        b->next->prev = rest;
    } else {
        last_block = rest;
    }
    b->next = rest;
    b->size = size;
}

// Merge b with the block after it (both must be free).
static void merge_with_next(struct block *b) {
    struct block *n = b->next;
    b->size += HEADER + n->size;
    b->next = n->next;
    if (n->next != NULL) {
        n->next->prev = b;
    } else {
        last_block = b;
    }
    n->magic = 0;   // no longer a block
}

void *kmalloc(size_t size) {
    if (size == 0 || size > VIRT_HEAP_MAX) {
        return NULL;
    }
    uint64_t need = ALIGN_UP(size, ALIGNMENT);

    bool was_enabled = cpu_irq_save();
    for (int attempt = 0; attempt < 2; attempt++) {
        for (struct block *b = first_block; b != NULL; b = b->next) {
            check_block(b, "kmalloc");
            if (b->magic == MAGIC_FREE && b->size >= need) {
                split(b, need);
                b->magic = MAGIC_USED;
                used_bytes += b->size;
                allocations++;
                cpu_irq_restore(was_enabled);
                return payload(b);
            }
        }
        // Nothing fits: grow (enough for the block and its header) and search again.
        if (attempt == 0 && !heap_grow(need + HEADER)) {
            break;
        }
    }
    cpu_irq_restore(was_enabled);
    return NULL;
}

void *kzalloc(size_t size) {
    void *p = kmalloc(size);
    if (p != NULL) {
        memset(p, 0, size);
    }
    return p;
}

void kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    uint64_t addr = (uint64_t)ptr;
    if (addr < VIRT_HEAP_START + HEADER || addr >= virt_mapped_end || !IS_ALIGNED(addr, ALIGNMENT)) {
        panic("kfree: %p is not a heap pointer", ptr);
    }

    bool was_enabled = cpu_irq_save();
    struct block *b = (struct block *)(addr - HEADER);
    if (b->magic == MAGIC_FREE) {
        panic("kfree: double free of %p", ptr);
    }
    if (b->magic != MAGIC_USED) {
        panic("kfree: %p has a bad header (magic %x): corrupted or not from kmalloc",
              ptr, b->magic);
    }
    used_bytes -= b->size;
    allocations--;
    b->magic = MAGIC_FREE;
    memset(payload(b), POISON_FREED, b->size);

    if (b->next != NULL && b->next->magic == MAGIC_FREE) {
        merge_with_next(b);
    }
    if (b->prev != NULL && b->prev->magic == MAGIC_FREE) {
        merge_with_next(b->prev);
    }
    cpu_irq_restore(was_enabled);
}

struct heap_stats heap_get_stats(void) {
    bool was_enabled = cpu_irq_save();
    struct heap_stats s = {
        .mapped_bytes = virt_mapped_end - VIRT_HEAP_START,
        .used_bytes = used_bytes,
        .allocations = allocations,
        .free_blocks = 0,
        .largest_free = 0,
    };
    for (struct block *b = first_block; b != NULL; b = b->next) {
        if (b->magic == MAGIC_FREE) {
            s.free_blocks++;
            if (b->size > s.largest_free) {
                s.largest_free = b->size;
            }
        }
    }
    cpu_irq_restore(was_enabled);
    return s;
}

bool heap_check(void) {
    bool was_enabled = cpu_irq_save();
    bool ok = true;
    uint8_t *expected = (uint8_t *)VIRT_HEAP_START;
    for (struct block *b = first_block; b != NULL; b = b->next) {
        if (b->magic != MAGIC_USED && b->magic != MAGIC_FREE) {
            kprintf("heap_check: bad magic at %p\n", (void *)b);
            ok = false;
            break;
        }
        if ((uint8_t *)b != expected) {
            kprintf("heap_check: gap/overlap at %p (expected %p)\n", (void *)b, (void *)expected);
            ok = false;
        }
        if (b->next != NULL && b->next->prev != b) {
            kprintf("heap_check: broken links at %p\n", (void *)b);
            ok = false;
        }
        if (b->magic == MAGIC_FREE && b->next != NULL && b->next->magic == MAGIC_FREE) {
            kprintf("heap_check: two free blocks in a row at %p (not merged)\n", (void *)b);
            ok = false;
        }
        expected = block_end(b);
    }
    if (first_block != NULL && expected != (uint8_t *)virt_mapped_end) {
        kprintf("heap_check: blocks end at %p, heap ends at %p\n",
                (void *)expected, (void *)virt_mapped_end);
        ok = false;
    }
    cpu_irq_restore(was_enabled);
    return ok;
}
