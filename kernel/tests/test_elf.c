// Tests for Stage 9: ELF loader, argv, spawn/wait, user libc.

#include <stdint.h>
#include <stdbool.h>
#include "tests/tests.h"
#include "tests/ktest.h"
#include "lib/kprintf.h"
#include "lib/string.h"
#include "sched/task.h"
#include "proc/process.h"
#include "proc/elf.h"
#include "fs/vfs.h"
#include "mm/heap.h"
#include "mm/pmm.h"
#include "mm/vmm.h"

// Spawn an ELF from the VFS as our child and wait for it. Returns its exit code.
static int run_elf(const char *path, int argc, const char *const *argv) {
    const char *error;
    struct task *t = process_spawn_elf(path, argc, argv, task_current(), &error);
    if (t == NULL) {
        kprintf("spawn %s failed: %s\n", path, error);
        return -1000;
    }
    int code = -1;
    task_wait(t->id, &code);
    return code;
}

// Done test of Stage 9: a separately compiled program from the initrd runs.
void test_elf_hello(void) {
    const char *argv[] = { "/bin/hello", "first", "second arg" };
    int code = run_elf("/bin/hello", 3, argv);
    kprintf("kernel: /bin/hello exited with code %d\n", code);
    ktest_finish(code == 0);
}

void test_spawn(void) {
    const char *argv[] = { "spawntest" };
    int code = run_elf("/bin/spawntest", 1, argv);
    kprintf("kernel: spawntest exited with code %d\n", code);
    ktest_finish(code == 0);
}

// Broken or dangerous ELF files must be refused, not loaded.
static bool expect_refused(const char *what, const void *file, size_t size) {
    struct address_space *as = vmm_create_user_space();
    const char *error = "";
    uint64_t entry = elf_load(as, file, size, &error);
    vmm_destroy_user_space(as);
    kprintf("%s: %s\n", what, entry == 0 ? error : "LOADED (wrong!)");
    return entry == 0;
}

void test_elf_bad(void) {
    uint64_t size;
    uint8_t *good = vfs_read_all("/bin/hello", &size);
    uint8_t *bad = kmalloc(size);
    bool ok = true;

    ok &= expect_refused("text file", "just some text, not an ELF file", 31);

    memcpy(bad, good, size);
    ok &= expect_refused("cut after 40 bytes", bad, 40);

    // First PT_LOAD moved into the kernel half.
    memcpy(bad, good, size);
    struct elf64_header *h = (struct elf64_header *)bad;
    struct elf64_phdr *ph = (struct elf64_phdr *)(bad + h->phoff);
    ph[0].vaddr = 0xffffffff80000000ul;
    ok &= expect_refused("segment at a kernel address", bad, size);

    // filesz pointing past the end of the file.
    memcpy(bad, good, size);
    ph[0].filesz = size * 10;
    ph[0].memsz = size * 10;
    ok &= expect_refused("segment bigger than the file", bad, size);

    // Entry point in a non-executable segment.
    memcpy(bad, good, size);
    h->entry = ph[1].vaddr;
    ok &= expect_refused("entry point in rodata", bad, size);

    kfree(bad);
    kfree(good);
    ktest_finish(ok);
}

void test_elf_leak(void) {
    const char *argv[] = { "exitcode", "7" };
    run_elf("/bin/exitcode", 2, argv);   // warm-up
    uint64_t free_before = pmm_get_stats().free_pages;
    uint64_t allocs_before = heap_get_stats().allocations;
    bool codes_ok = true;
    for (int i = 0; i < 10; i++) {
        codes_ok &= run_elf("/bin/exitcode", 2, argv) == 7;
    }
    task_sleep_ms(20);
    long page_leak = (long)(free_before - pmm_get_stats().free_pages);
    long alloc_leak = (long)(heap_get_stats().allocations - allocs_before);
    kprintf("10 x spawn+wait: exit codes %s, page leak %ld, heap leak %ld\n",
            codes_ok ? "ok" : "WRONG", page_leak, alloc_leak);
    ktest_finish(codes_ok && page_leak == 0 && alloc_leak == 0);
}
