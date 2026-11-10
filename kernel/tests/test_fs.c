// Tests for Stage 8: VFS, tarfs (initrd), devfs, file syscalls.

#include <stdint.h>
#include <stdbool.h>
#include "tests/tests.h"
#include "tests/ktest.h"
#include "lib/kprintf.h"
#include "lib/string.h"
#include "fs/vfs.h"
#include "mm/heap.h"
#include "syscall/syscall_nr.h"

static void print_tree(struct vfs_node *dir, int depth) {
    for (uint64_t i = 0;; i++) {
        struct vfs_node *n = vfs_readdir(dir, i);
        if (n == NULL) {
            break;
        }
        for (int d = 0; d < depth; d++) {
            kprintf("  ");
        }
        if (n->type == VFS_DIR) {
            kprintf("%s/\n", n->name);
            print_tree(n, depth + 1);
        } else if (n->type == VFS_DEV) {
            kprintf("%s (device)\n", n->name);
        } else {
            kprintf("%s (%lu bytes)\n", n->name, n->size);
        }
    }
}

void test_vfs(void) {
    bool ok = true;
    kprintf("tree of /:\n");
    print_tree(vfs_root(), 1);

    // Read /hello.txt in the kernel.
    uint64_t size;
    char *text = vfs_read_all("/hello.txt", &size);
    kprintf("/hello.txt (%lu bytes): %s", size, text ? text : "(missing)\n");
    ok = ok && text != NULL && memcmp(text, "Hello from the initrd!", 22) == 0;
    kfree(text);

    // Path handling: ".", "..", repeated slashes.
    struct vfs_node *a = vfs_lookup("/etc/motd");
    struct vfs_node *b = vfs_lookup("//docs/../etc/./motd");
    kprintf("lookup with . and ..: %s\n", (a != NULL && a == b) ? "same node" : "WRONG");
    ok = ok && a != NULL && a == b;

    // Errors.
    int64_t err = 0;
    ok = ok && vfs_open("/missing", O_RDONLY, &err) == NULL && err == -ENOENT;
    ok = ok && vfs_open("/hello.txt", O_WRONLY, &err) == NULL && err == -EROFS;
    ok = ok && vfs_open("/etc", O_WRONLY, &err) == NULL && err == -EISDIR;
    ok = ok && vfs_lookup("/hello.txt/x") == NULL;
    kprintf("error cases (ENOENT, EROFS, EISDIR, file as dir): %s\n", ok ? "ok" : "WRONG");

    // Reading in small pieces gives the same bytes as one big read.
    struct file *f = vfs_open("/docs/about.txt", O_RDONLY, &err);
    char piece[7];
    uint64_t total = 0;
    int64_t n;
    while ((n = vfs_read(f, piece, sizeof(piece))) > 0) {
        total += (uint64_t)n;
    }
    kprintf("read about.txt in 7-byte pieces: %lu bytes (size %lu)\n", total, f->node->size);
    ok = ok && total == f->node->size;
    vfs_close(f);

    // /dev/null
    f = vfs_open("/dev/null", O_RDWR, &err);
    ok = ok && f != NULL && vfs_write(f, "abc", 3) == 3 && vfs_read(f, piece, 4) == 0;
    vfs_close(f);

    ktest_finish(ok);
}

// ---------------------------------------------------------------------------
// Disk: ATA PIO + FAT32
// ---------------------------------------------------------------------------

#include "drivers/ata.h"

// Read and check /disk/numbers.txt = "1\n2\n...2000\n" (spans many clusters).
static bool check_numbers(void) {
    uint64_t size;
    char *text = vfs_read_all("/disk/numbers.txt", &size);
    if (text == NULL) {
        return false;
    }
    uint64_t expected = 1, value = 0;
    bool ok = true;
    for (uint64_t i = 0; i < size; i++) {
        if (text[i] == '\n') {
            if (value != expected) {
                kprintf("numbers.txt: line %lu is %lu\n", expected, value);
                ok = false;
                break;
            }
            expected++;
            value = 0;
        } else {
            value = value * 10 + (uint64_t)(text[i] - '0');
        }
    }
    kprintf("numbers.txt: %lu bytes, lines 1..%lu in order: %s\n",
            size, expected - 1, ok && expected == 2001 ? "ok" : "WRONG");
    kfree(text);
    return ok && expected == 2001;
}

void test_disk(void) {
    bool ok = true;
    kprintf("tree of /disk:\n");
    print_tree(vfs_lookup("/disk"), 1);

    const char *files[] = { "/disk/readme.txt", "/disk/welcome-to-the-disk.txt",
                            "/disk/docs/nested-file.txt" };
    for (int i = 0; i < 3; i++) {
        uint64_t size;
        char *text = vfs_read_all(files[i], &size);
        kprintf("%s: %s", files[i], text ? text : "MISSING\n");
        ok = ok && text != NULL;
        kfree(text);
    }

    ok = check_numbers() && ok;

    // Reads that start in the middle of a cluster and cross into the next one.
    int64_t err;
    struct file *f = vfs_open("/disk/numbers.txt", O_RDONLY, &err);
    char a[100], b[100];
    f->offset = 1000;                     // cluster 1 (512-byte clusters), offset 488
    int64_t n = vfs_read(f, a, sizeof(a));
    uint64_t size;
    char *all = vfs_read_all("/disk/numbers.txt", &size);
    memcpy(b, all + 1000, sizeof(b));
    bool cross = n == (int64_t)sizeof(a) && memcmp(a, b, sizeof(a)) == 0;
    kprintf("read across a cluster boundary: %s\n", cross ? "ok" : "WRONG");
    ok = ok && cross;
    kfree(all);
    vfs_close(f);

    ktest_finish(ok);
}

// Raw sectors on the scratch disk (ATA drive 1): write, read back, compare.
void test_ata(void) {
    static uint8_t out[4 * ATA_SECTOR_SIZE], in[4 * ATA_SECTOR_SIZE];
    for (unsigned i = 0; i < sizeof(out); i++) {
        out[i] = (uint8_t)(i * 7 + 3);
    }
    bool wrote = ata_write(1, 10, 4, out);
    memset(in, 0, sizeof(in));
    bool read = ata_read(1, 10, 4, in);
    bool same = memcmp(in, out, sizeof(out)) == 0;
    kprintf("ata: wrote 4 sectors at LBA 10: %s, read back: %s, data %s\n",
            wrote ? "ok" : "FAILED", read ? "ok" : "FAILED", same ? "identical" : "DIFFERENT");
    bool range = !ata_read(1, 2047, 2, in);   // last sector + 1: past the end
    kprintf("ata: read past the end of the disk refused: %s\n", range ? "yes" : "NO");
    ktest_finish(wrote && read && same && range);
}
