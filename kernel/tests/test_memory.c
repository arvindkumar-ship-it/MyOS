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
