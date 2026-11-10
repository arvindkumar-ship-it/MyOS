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
