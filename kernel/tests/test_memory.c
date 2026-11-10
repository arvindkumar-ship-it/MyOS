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
