#include "fs/devfs.h"

#include "fs/vfs.h"
#include "drivers/input.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

static int64_t console_read(struct vfs_node *node, uint64_t offset, void *buf, uint64_t len) {
    (void)node;
    (void)offset;
    if (len == 0) {
        return 0;
    }
    char *dst = buf;
    // Wait for the first character, then take whatever else is already there.
    uint64_t n = 0;
    dst[n++] = input_getc();
    char c;
    while (n < len && input_try_getc(&c)) {
        dst[n++] = c;
    }
    return (int64_t)n;
}

static int64_t console_write(struct vfs_node *node, uint64_t offset, const void *buf, uint64_t len) {
    (void)node;
    (void)offset;
    const char *s = buf;
    for (uint64_t i = 0; i < len; i++) {
        kprintf("%c", s[i]);
    }
    return (int64_t)len;
}

static int64_t null_read(struct vfs_node *node, uint64_t offset, void *buf, uint64_t len) {
    (void)node; (void)offset; (void)buf; (void)len;
    return 0;
}

static int64_t null_write(struct vfs_node *node, uint64_t offset, const void *buf, uint64_t len) {
    (void)node; (void)offset; (void)buf;
    return (int64_t)len;
}

static const struct vfs_ops console_ops = { .read = console_read, .write = console_write };
static const struct vfs_ops null_ops = { .read = null_read, .write = null_write };

void devfs_init(void) {
    struct vfs_node *dev = vfs_mkdir_p("/dev");
    if (dev == NULL
        || vfs_create_node(dev, "console", VFS_DEV, &console_ops) == NULL
        || vfs_create_node(dev, "null", VFS_DEV, &null_ops) == NULL) {
        panic("devfs_init: out of memory");
    }
}
