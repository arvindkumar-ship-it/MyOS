#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Virtual File System: one tree of files and directories that the rest of
// the kernel (and the syscalls) use, no matter which filesystem a file is
// really on. Each filesystem plugs in through a table of functions (vfs_ops).
//
//   /                 root directory (in memory)
//   /hello.txt, ...   from the initrd (tarfs, read only)
//   /dev/console      the screen + keyboard (devfs)
//   /disk/...         a FAT32 disk (Stage 8, second part)

#define VFS_NAME_MAX 64

enum vfs_type {
    VFS_FILE = 1,
    VFS_DIR  = 2,
    VFS_DEV  = 3,   // character device (console, null)
};

struct vfs_node;

// What a filesystem must provide. Any function may be NULL (= not supported).
struct vfs_ops {
    // Read/write up to len bytes at offset. Return bytes done, or -errno.
    int64_t (*read)(struct vfs_node *node, uint64_t offset, void *buf, uint64_t len);
    int64_t (*write)(struct vfs_node *node, uint64_t offset, const void *buf, uint64_t len);
    // Directories: find a child by name (NULL if none) ...
    struct vfs_node *(*lookup)(struct vfs_node *dir, const char *name);
    // ... and list them: the index-th child, or NULL after the last one.
    struct vfs_node *(*readdir)(struct vfs_node *dir, uint64_t index);
};

struct vfs_node {
    char name[VFS_NAME_MAX];
    enum vfs_type type;
    uint64_t size;                 // bytes, for files
    const struct vfs_ops *ops;
    void *priv;                    // filesystem private data
    uint64_t priv2;
    struct vfs_node *parent;
    // In-memory directories (and cached nodes of other filesystems) keep
    // their children in this list.
    struct vfs_node *first_child;
    struct vfs_node *next_sibling;
};

// An open file: a node plus the current position. Shared by fds that point to it.
struct file {
    struct vfs_node *node;
    uint64_t offset;
    int flags;
    int refcount;
};

// open() flags
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2

// Create the root directory.
void vfs_init(void);

struct vfs_node *vfs_root(void);

// New node in memory under "parent" (parent may be NULL for a detached node).
// Uses the generic in-memory directory ops for VFS_DIR.
struct vfs_node *vfs_create_node(struct vfs_node *parent, const char *name, enum vfs_type type,
                                 const struct vfs_ops *ops);

// Make the directory at "path" (and missing parents, like mkdir -p).
struct vfs_node *vfs_mkdir_p(const char *path);

// Find a node by absolute path ("/a/b/c"; "." and ".." work). NULL if missing.
struct vfs_node *vfs_lookup(const char *path);

// Child of a directory by name / by index (using the directory's ops).
struct vfs_node *vfs_lookup_child(struct vfs_node *dir, const char *name);
struct vfs_node *vfs_readdir(struct vfs_node *dir, uint64_t index);

// Generic in-memory directory ops (children list). Filesystems can reuse
// them for directories they build fully in memory (tarfs, devfs).
extern const struct vfs_ops vfs_memdir_ops;

// --- Open files ---

// Open a path. Returns a file (refcount 1) or NULL and sets *err to -errno.
struct file *vfs_open(const char *path, int flags, int64_t *err);
int64_t vfs_read(struct file *f, void *buf, uint64_t len);
int64_t vfs_write(struct file *f, const void *buf, uint64_t len);
void vfs_file_ref(struct file *f);
void vfs_close(struct file *f);    // drops one reference; frees at 0

// Read a whole file into a new kmalloc'ed buffer (for the kernel).
// Returns NULL if missing. *size gets the length.
void *vfs_read_all(const char *path, uint64_t *size);
