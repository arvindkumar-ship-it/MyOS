#include "fs/vfs.h"

#include "mm/heap.h"
#include "lib/string.h"
#include "lib/kprintf.h"
#include "lib/panic.h"
#include "syscall/syscall_nr.h"

static struct vfs_node *root;

// ---------------------------------------------------------------------------
// Generic in-memory directory: children in a linked list.
// ---------------------------------------------------------------------------

static bool name_equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static struct vfs_node *memdir_lookup(struct vfs_node *dir, const char *name) {
    for (struct vfs_node *c = dir->first_child; c != NULL; c = c->next_sibling) {
        if (name_equal(c->name, name)) {
            return c;
        }
    }
    return NULL;
}

static struct vfs_node *memdir_readdir(struct vfs_node *dir, uint64_t index) {
    struct vfs_node *c = dir->first_child;
    while (c != NULL && index > 0) {
        c = c->next_sibling;
        index--;
    }
    return c;
}

const struct vfs_ops vfs_memdir_ops = {
    .lookup = memdir_lookup,
    .readdir = memdir_readdir,
};

struct vfs_node *vfs_create_node(struct vfs_node *parent, const char *name, enum vfs_type type,
                                 const struct vfs_ops *ops) {
    struct vfs_node *n = kzalloc(sizeof(*n));
    if (n == NULL) {
        return NULL;
    }
    size_t i = 0;
    for (; i < VFS_NAME_MAX - 1 && name[i] != '\0'; i++) {
        n->name[i] = name[i];
    }
    n->name[i] = '\0';
    n->type = type;
    n->ops = ops != NULL ? ops : (type == VFS_DIR ? &vfs_memdir_ops : NULL);
    n->parent = parent;
    if (parent != NULL) {
        // Append, so directory listings keep the order things were added in.
        struct vfs_node **pp = &parent->first_child;
        while (*pp != NULL) {
            pp = &(*pp)->next_sibling;
        }
        *pp = n;
    }
    return n;
}

void vfs_init(void) {
    root = vfs_create_node(NULL, "", VFS_DIR, NULL);
    if (root == NULL) {
        panic("vfs_init: out of memory");
    }
    root->parent = root;   // ".." of "/" is "/"
}

struct vfs_node *vfs_root(void) {
    return root;
}

struct vfs_node *vfs_lookup_child(struct vfs_node *dir, const char *name) {
    if (dir->type != VFS_DIR || dir->ops == NULL || dir->ops->lookup == NULL) {
        return NULL;
    }
    return dir->ops->lookup(dir, name);
}

struct vfs_node *vfs_readdir(struct vfs_node *dir, uint64_t index) {
    if (dir->type != VFS_DIR || dir->ops == NULL || dir->ops->readdir == NULL) {
        return NULL;
    }
    return dir->ops->readdir(dir, index);
}

// Walk "path" one component at a time. "create_dirs": make missing directories.
static struct vfs_node *walk(const char *path, bool create_dirs) {
    if (path == NULL || path[0] != '/') {
        return NULL;   // only absolute paths
    }
    struct vfs_node *node = root;
    const char *p = path;
    char part[VFS_NAME_MAX];

    while (*p != '\0') {
        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        size_t len = 0;
        while (*p != '\0' && *p != '/') {
            if (len == VFS_NAME_MAX - 1) {
                return NULL;   // name too long
            }
            part[len++] = *p++;
        }
        part[len] = '\0';

        if (node->type != VFS_DIR) {
            return NULL;   // "file.txt/something"
        }
        if (name_equal(part, ".")) {
            continue;
        }
        if (name_equal(part, "..")) {
            node = node->parent;
            continue;
        }
        struct vfs_node *child = vfs_lookup_child(node, part);
        if (child == NULL && create_dirs) {
            child = vfs_create_node(node, part, VFS_DIR, NULL);
        }
        if (child == NULL) {
            return NULL;
        }
        node = child;
    }
    return node;
}

struct vfs_node *vfs_lookup(const char *path) {
    return walk(path, false);
}

struct vfs_node *vfs_mkdir_p(const char *path) {
    return walk(path, true);
}

// ---------------------------------------------------------------------------
// Open files
// ---------------------------------------------------------------------------

struct file *vfs_open(const char *path, int flags, int64_t *err) {
    struct vfs_node *node = vfs_lookup(path);
    if (node == NULL) {
        *err = -ENOENT;
        return NULL;
    }
    int mode = flags & 3;
    if (node->type == VFS_DIR && mode != O_RDONLY) {
        *err = -EISDIR;
        return NULL;
    }
    if (mode != O_RDONLY && (node->ops == NULL || node->ops->write == NULL)) {
        *err = -EROFS;
        return NULL;
    }
    struct file *f = kzalloc(sizeof(*f));
    if (f == NULL) {
        *err = -ENOMEM;
        return NULL;
    }
    f->node = node;
    f->flags = flags;
    f->refcount = 1;
    return f;
}

int64_t vfs_read(struct file *f, void *buf, uint64_t len) {
    struct vfs_node *n = f->node;
    if (n->type == VFS_DIR) {
        return -EISDIR;
    }
    if ((f->flags & 3) == O_WRONLY) {
        return -EBADF;
    }
    if (n->ops == NULL || n->ops->read == NULL) {
        return -EINVAL;
    }
    int64_t done = n->ops->read(n, f->offset, buf, len);
    if (done > 0 && n->type == VFS_FILE) {
        f->offset += (uint64_t)done;   // devices have no position
    }
    return done;
}

int64_t vfs_write(struct file *f, const void *buf, uint64_t len) {
    struct vfs_node *n = f->node;
    if ((f->flags & 3) == O_RDONLY) {
        return -EBADF;
    }
    if (n->ops == NULL || n->ops->write == NULL) {
        return -EROFS;
    }
    int64_t done = n->ops->write(n, f->offset, buf, len);
    if (done > 0 && n->type == VFS_FILE) {
        f->offset += (uint64_t)done;
    }
    return done;
}

void vfs_file_ref(struct file *f) {
    f->refcount++;
}

void vfs_close(struct file *f) {
    if (--f->refcount == 0) {
        kfree(f);
    }
}

void *vfs_read_all(const char *path, uint64_t *size) {
    int64_t err;
    struct file *f = vfs_open(path, O_RDONLY, &err);
    if (f == NULL) {
        return NULL;
    }
    uint64_t len = f->node->size;
    uint8_t *buf = kmalloc(len + 1);   // +1: room for a '\0' for text files
    if (buf == NULL) {
        vfs_close(f);
        return NULL;
    }
    int64_t got = vfs_read(f, buf, len);
    vfs_close(f);
    if (got < 0) {
        kfree(buf);
        return NULL;
    }
    buf[got] = '\0';
    *size = (uint64_t)got;
    return buf;
}
