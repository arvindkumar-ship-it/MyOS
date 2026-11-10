// Tests for Stage 5: PMM, VMM, heap.

#include <stdint.h>
#include <stdbool.h>
#include "tests/tests.h"
#include "tests/ktest.h"
#include "lib/kprintf.h"
#include "lib/string.h"
#include "mm/memory.h"
#include "mm/pmm.h"

#define TEST_PAGES 1000

static uint64_t test_phys[TEST_PAGES];

// Done test of Stage 5 (PMM part): allocate 1000 pages, use them, free them,
// and check that exactly as many pages are free as before (no leak).
void test_pmm(void) {
    struct pmm_stats before = pmm_get_stats();
    kprintf("pmm: %lu pages free before\n", before.free_pages);

    for (int i = 0; i < TEST_PAGES; i++) {
        uint64_t phys = pmm_alloc_page();
        if (phys == 0 || !IS_ALIGNED(phys, PAGE_SIZE)) {
            kprintf("bad page %d: %p\n", i, (void *)phys);
            ktest_finish(false);
        }
        test_phys[i] = phys;
        // Write a pattern through the HHDM: the page must really be usable RAM.
        uint64_t *virt = phys_to_virt(phys);
        virt[0] = phys;
        virt[PAGE_SIZE / 8 - 1] = ~phys;
    }

    // No page handed out twice, and every pattern still intact.
    for (int i = 0; i < TEST_PAGES; i++) {
        uint64_t *virt = phys_to_virt(test_phys[i]);
        if (virt[0] != test_phys[i] || virt[PAGE_SIZE / 8 - 1] != ~test_phys[i]) {
            kprintf("page %p overwritten: handed out twice?\n", (void *)test_phys[i]);
            ktest_finish(false);
        }
    }
    struct pmm_stats during = pmm_get_stats();
    kprintf("pmm: allocated %d pages, %lu free now (diff %lu)\n",
            TEST_PAGES, during.free_pages, before.free_pages - during.free_pages);

    for (int i = 0; i < TEST_PAGES; i++) {
        pmm_free_page(test_phys[i]);
    }

    // Contiguous allocation too.
    uint64_t run = pmm_alloc_pages(64);
    kprintf("pmm: 64 contiguous pages at phys %p\n", (void *)run);
    pmm_free_pages(run, 64);

    struct pmm_stats after = pmm_get_stats();
    kprintf("pmm: %lu pages free after, leak = %ld pages\n",
            after.free_pages, (long)(before.free_pages - after.free_pages));
    ktest_finish(run != 0 && after.free_pages == before.free_pages
                 && before.free_pages - during.free_pages == TEST_PAGES);
}

// Freeing a page twice must be caught.
void test_pmm_double_free(void) {
    uint64_t phys = pmm_alloc_page();
    pmm_free_page(phys);
    kprintf("freeing phys %p a second time...\n", (void *)phys);
    pmm_free_page(phys);
    ktest_finish(false);
}

// ---------------------------------------------------------------------------
// VMM
// ---------------------------------------------------------------------------

#include "mm/vmm.h"

#define VMM_TEST_PAGES 1000

// Map 1000 fresh pages at a test address, use them through the NEW virtual
// address, check the translation, unmap, and check nothing leaked.
void test_vmm(void) {
    struct address_space *as = vmm_kernel_space();
    uint64_t virt = VIRT_TEST_START;

    // Print the permissions our page tables gave the kernel image.
    extern char __kernel_text_start[], __kernel_rodata_start[], __kernel_data_start[];
    kprintf("flags: text %lx, rodata %lx, data %lx\n",
            vmm_get_flags(as, (uint64_t)__kernel_text_start),
            vmm_get_flags(as, (uint64_t)__kernel_rodata_start),
            vmm_get_flags(as, (uint64_t)__kernel_data_start));

    // Warm-up round: the first mapping in a new area allocates page tables,
    // which stay (we do not free empty tables). Measure leaks after it.
    vmm_alloc_pages(as, virt, VMM_TEST_PAGES, PTE_WRITE | PTE_NX);
    vmm_free_pages(as, virt, VMM_TEST_PAGES);

    uint64_t free_before = pmm_get_stats().free_pages;
    uint64_t tables_before = vmm_table_pages();

    if (!vmm_alloc_pages(as, virt, VMM_TEST_PAGES, PTE_WRITE | PTE_NX)) {
        kprintf("vmm_alloc_pages failed\n");
        ktest_finish(false);
    }
    for (uint64_t i = 0; i < VMM_TEST_PAGES; i++) {
        uint64_t *virt_page = (uint64_t *)(virt + i * PAGE_SIZE);
        virt_page[0] = 0xC0FFEE00 + i;               // write through the new mapping
        uint64_t phys = vmm_translate(as, (uint64_t)virt_page);
        uint64_t *alias = phys_to_virt(phys);         // same RAM, seen through the HHDM
        if (phys == (uint64_t)-1 || alias[0] != 0xC0FFEE00 + i) {
            kprintf("page %lu: translation wrong (phys %p)\n", i, (void *)phys);
            ktest_finish(false);
        }
    }
    kprintf("vmm: 1000 pages mapped at %p, written and checked via HHDM\n", (void *)virt);

    vmm_free_pages(as, virt, VMM_TEST_PAGES);
    bool unmapped = vmm_translate(as, virt) == (uint64_t)-1;

    uint64_t free_after = pmm_get_stats().free_pages;
    kprintf("vmm: unmapped=%s, leak = %ld pages, table pages %lu -> %lu\n",
            unmapped ? "yes" : "no", (long)(free_before - free_after),
            tables_before, vmm_table_pages());
    ktest_finish(unmapped && free_before == free_after && tables_before == vmm_table_pages());
}

// Done test of Stage 5 (page fault part): touch a page after unmapping it.
// The page fault handler must print exactly that address.
void test_vmm_unmapped(void) {
    struct address_space *as = vmm_kernel_space();
    uint64_t virt = VIRT_TEST_START + 0x5000;
    vmm_alloc_pages(as, virt, 1, PTE_WRITE | PTE_NX);
    *(volatile uint64_t *)virt = 42;
    kprintf("wrote to %p, unmapping it and writing again...\n", (void *)virt);
    vmm_free_pages(as, virt, 1);
    *(volatile uint64_t *)virt = 43;   // must fault (stale TLB entry would hide it)
    kprintf("not reached\n");
    ktest_finish(false);
}

// Code in a no-execute (data) page must not run.
void test_nx(void) {
    struct address_space *as = vmm_kernel_space();
    uint64_t virt = VIRT_TEST_START + 0x9000;
    vmm_alloc_pages(as, virt, 1, PTE_WRITE | PTE_NX);
    *(volatile uint8_t *)virt = 0xC3;   // x86 "ret" instruction
    kprintf("calling code in an NX page at %p...\n", (void *)virt);
    ((void (*)(void))virt)();
    kprintf("not reached\n");
    ktest_finish(false);
}

// Writing to .rodata must fault, even in ring 0 (CR0.WP).
void test_wp(void) {
    static const char message[] = "read only";
    kprintf("writing to .rodata at %p...\n", (void *)message);
    *(volatile char *)message = 'X';
    kprintf("not reached\n");
    ktest_finish(false);
}

// NULL pointer: nothing is mapped at address 0.
void test_null(void) {
    volatile uint64_t *ptr = (volatile uint64_t *)0;
    kprintf("reading NULL...\n");
    uint64_t value = *ptr;
    kprintf("not reached: %lx\n", value);
    ktest_finish(false);
}

// ---------------------------------------------------------------------------
// Heap
// ---------------------------------------------------------------------------

#include "mm/heap.h"

#define HEAP_SLOTS 500
#define HEAP_ROUNDS 5000

static uint8_t *slot_ptr[HEAP_SLOTS];
static uint32_t slot_size[HEAP_SLOTS];

// Small pseudo random number generator (xorshift), so the test is repeatable.
static uint64_t rng_state = 0x9E3779B97F4A7C15ul;
static uint64_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static bool slot_intact(int i) {
    for (uint32_t j = 0; j < slot_size[i]; j++) {
        if (slot_ptr[i][j] != (uint8_t)i) {
            return false;
        }
    }
    return true;
}

void test_heap(void) {
    struct heap_stats start = heap_get_stats();

    // Random kmalloc/kfree. Every block is filled with its slot number, and
    // checked before it is freed: two blocks overlapping would break this.
    for (int round = 0; round < HEAP_ROUNDS; round++) {
        int i = (int)(rng() % HEAP_SLOTS);
        if (slot_ptr[i] != NULL) {
            if (!slot_intact(i)) {
                kprintf("slot %d overwritten (overlapping blocks?)\n", i);
                ktest_finish(false);
            }
            kfree(slot_ptr[i]);
            slot_ptr[i] = NULL;
        } else {
            // Mostly small sizes, sometimes a big one.
            uint32_t size = (rng() % 8 == 0) ? (uint32_t)(rng() % 65536) + 1
                                             : (uint32_t)(rng() % 512) + 1;
            uint8_t *p = kmalloc(size);
            if (p == NULL || !IS_ALIGNED((uint64_t)p, 16)) {
                kprintf("kmalloc(%u) returned %p\n", size, (void *)p);
                ktest_finish(false);
            }
            memset(p, (uint8_t)i, size);
            slot_ptr[i] = p;
            slot_size[i] = size;
        }
    }
    struct heap_stats mid = heap_get_stats();
    kprintf("heap: after %d random ops: %lu live allocations, %lu KiB used, "
            "%lu KiB mapped, %lu free blocks\n", HEAP_ROUNDS, mid.allocations,
            mid.used_bytes / 1024, mid.mapped_bytes / 1024, mid.free_blocks);

    for (int i = 0; i < HEAP_SLOTS; i++) {
        if (slot_ptr[i] != NULL) {
            if (!slot_intact(i)) {
                kprintf("slot %d overwritten\n", i);
                ktest_finish(false);
            }
            kfree(slot_ptr[i]);
            slot_ptr[i] = NULL;
        }
    }

    struct heap_stats end = heap_get_stats();
    bool check = heap_check();
    kprintf("heap: all freed: leaked %ld allocations / %ld bytes, %lu free block(s), heap_check %s\n",
            (long)(end.allocations - start.allocations), (long)(end.used_bytes - start.used_bytes),
            end.free_blocks, check ? "ok" : "FAILED");

    // Everything merged back into one block: a request for all of it must
    // succeed without growing the heap.
    uint64_t mapped = end.mapped_bytes;
    void *all = kmalloc(end.largest_free);
    bool reused = all != NULL && heap_get_stats().mapped_bytes == mapped;
    kfree(all);
    kprintf("heap: one kmalloc of the whole free block (%lu KiB): %s\n",
            end.largest_free / 1024, reused ? "ok, no growth" : "FAILED");

    // Something bigger than the heap: it must grow.
    void *big = kmalloc(3 * 1024 * 1024);
    bool grew = big != NULL && heap_get_stats().mapped_bytes > mapped;
    kfree(big);
    kprintf("heap: 3 MiB kmalloc %s\n", grew ? "grew the heap" : "FAILED");

    ktest_finish(check && end.allocations == start.allocations && end.used_bytes == start.used_bytes
                 && end.free_blocks == 1 && reused && grew && heap_check());
}

void test_heap_double_free(void) {
    void *p = kmalloc(100);
    kfree(p);
    kprintf("freeing %p again...\n", p);
    kfree(p);
    ktest_finish(false);
}

// Writing past the end of a block destroys the next block's header.
void test_heap_overflow(void) {
    uint8_t *a = kmalloc(32);
    uint8_t *b = kmalloc(32);
    kprintf("writing 64 bytes into a 32 byte block...\n");
    memset(a, 'X', 64);
    kfree(b);
    ktest_finish(false);
}
