#include "fs/tarfs.h"

#include "lib/string.h"
#include "lib/kprintf.h"

// A tar archive is a list of 512-byte blocks. Each file is one header block,
// then its data rounded up to whole blocks. Two zero blocks end the archive.
// Numbers in the header are ASCII octal strings (a leftover from 1979).
struct ustar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];        // octal
    char mtime[12];
    char checksum[8];     // octal: sum of all header bytes, this field counted as spaces
    char typeflag;        // '0' or '\0' = file, '5' = directory
    char linkname[100];
    char magic[6];        // "ustar"
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];     // long names: path = prefix + "/" + name
    char pad[12];
} __attribute__((packed));

_Static_assert(sizeof(struct ustar_header) == 512, "tar header must be 512 bytes");

#define TAR_BLOCK 512
#define ALIGN_UP_512(x) (((x) + 511) & ~(uint64_t)511)

static uint64_t parse_octal(const char *s, size_t len) {
    uint64_t value = 0;
    for (size_t i = 0; i < len && s[i] >= '0' && s[i] <= '7'; i++) {
        value = value * 8 + (uint64_t)(s[i] - '0');
    }
    return value;
}

static bool checksum_ok(const struct ustar_header *h) {
    const uint8_t *bytes = (const uint8_t *)h;
    uint64_t sum = 0;
    for (size_t i = 0; i < TAR_BLOCK; i++) {
        bool in_field = i >= 148 && i < 156;   // the checksum field itself
        sum += in_field ? ' ' : bytes[i];
    }
    return sum == parse_octal(h->checksum, sizeof(h->checksum));
}

static bool all_zero(const uint8_t *block) {
    for (size_t i = 0; i < TAR_BLOCK; i++) {
        if (block[i] != 0) {
            return false;
        }
    }
    return true;
}

// File contents: priv = start of the data inside the archive.
static int64_t tarfs_read(struct vfs_node *node, uint64_t offset, void *buf, uint64_t len) {
    if (offset >= node->size) {
        return 0;   // end of file
    }
    if (len > node->size - offset) {
        len = node->size - offset;
    }
    memcpy(buf, (const uint8_t *)node->priv + offset, len);
    return (int64_t)len;
}

static const struct vfs_ops tarfs_file_ops = {
    .read = tarfs_read,
};

// Add "path" (relative, like "bin/hello") under dir, creating the directories
// on the way. Returns the final node (an existing one if it is already there).
static struct vfs_node *add_path(struct vfs_node *dir, const char *path, enum vfs_type type) {
    char part[VFS_NAME_MAX];
    const char *p = path;
    struct vfs_node *node = dir;
    while (*p != '\0') {
        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        size_t len = 0;
        while (*p != '\0' && *p != '/' && len < VFS_NAME_MAX - 1) {
            part[len++] = *p++;
        }
        part[len] = '\0';
        while (*p != '\0' && *p != '/') {
            p++;   // name too long: cut it
        }
        if (len == 1 && part[0] == '.') {
            continue;   // tar entries often start with "./"
        }
        bool last = true;
        for (const char *q = p; *q != '\0'; q++) {
            if (*q != '/') {
                last = false;
                break;
            }
        }
        struct vfs_node *child = vfs_lookup_child(node, part);
        if (child == NULL) {
            child = vfs_create_node(node, part, last ? type : VFS_DIR,
                                    (last && type == VFS_FILE) ? &tarfs_file_ops : NULL);
            if (child == NULL) {
                return NULL;
            }
        }
        node = child;
    }
    return node;
}

int tarfs_mount(struct vfs_node *dir, const void *virt_archive, uint64_t size) {
    const uint8_t *base = virt_archive;
    uint64_t pos = 0;
    int entries = 0;

    while (pos + TAR_BLOCK <= size) {
        const struct ustar_header *h = (const struct ustar_header *)(base + pos);
        if (all_zero((const uint8_t *)h)) {
            break;   // end of archive
        }
        if (memcmp(h->magic, "ustar", 5) != 0 || !checksum_ok(h)) {
            kprintf("tarfs: bad header at offset %lu\n", pos);
            return -1;
        }
        uint64_t file_size = parse_octal(h->size, sizeof(h->size));
        if (pos + TAR_BLOCK + file_size > size) {
            kprintf("tarfs: '%s' runs past the end of the archive\n", h->name);
            return -1;
        }

        // Full path = prefix + "/" + name (both fields may lack a '\0').
        char path[256];
        size_t len = 0;
        for (size_t i = 0; i < sizeof(h->prefix) && h->prefix[i] != '\0'; i++) {
            path[len++] = h->prefix[i];
        }
        if (len > 0) {
            path[len++] = '/';
        }
        for (size_t i = 0; i < sizeof(h->name) && h->name[i] != '\0'; i++) {
            path[len++] = h->name[i];
        }
        path[len] = '\0';

        if (h->typeflag == '5') {
            add_path(dir, path, VFS_DIR);
            entries++;
        } else if (h->typeflag == '0' || h->typeflag == '\0') {
            struct vfs_node *n = add_path(dir, path, VFS_FILE);
            if (n != NULL) {
                n->size = file_size;
                n->priv = (void *)(base + pos + TAR_BLOCK);
                entries++;
            }
        }
        // Other types (links, devices) are skipped.

        pos += TAR_BLOCK + ALIGN_UP_512(file_size);
    }
    return entries;
}
