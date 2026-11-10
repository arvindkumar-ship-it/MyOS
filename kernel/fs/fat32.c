#include "fs/fat32.h"

#include <stdint.h>
#include "drivers/ata.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "lib/kprintf.h"
#include "syscall/syscall_nr.h"

// FAT32 layout on disk:
//
//   sector 0            boot sector with the BPB (BIOS Parameter Block)
//   reserved sectors    (BPB.reserved_sectors, includes sector 0)
//   FAT #1, FAT #2      File Allocation Table: one 32-bit entry per cluster,
//                       holding the NEXT cluster of the same file (a linked list)
//   data area           clusters 2, 3, 4, ... (cluster = sectors_per_cluster sectors)
//
// A file or directory = first cluster number (in its directory entry) + the
// chain of clusters in the FAT. A directory's data is a list of 32-byte entries.

struct fat32_bpb {
    uint8_t  jump[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;       // 0 on FAT32
    uint16_t total_sectors16;    // 0 on FAT32
    uint8_t  media;
    uint16_t fat_size16;         // 0 on FAT32
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors32;
    // FAT32 extended part
    uint32_t fat_size32;         // sectors per FAT
    uint16_t ext_flags;
    uint16_t version;
    uint32_t root_cluster;       // first cluster of the root directory
    uint16_t fsinfo_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];         // "FAT32   "
} __attribute__((packed));

struct fat_dirent {
    char     name[11];           // 8.3 name, space padded, no dot
    uint8_t  attr;
    uint8_t  nt_reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_hi;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_lo;
    uint32_t size;
} __attribute__((packed));

// Long File Name entry: stored just before the 8.3 entry it belongs to,
// last part first. Each one carries 13 UCS-2 characters.
struct fat_lfn {
    uint8_t  order;              // part number (1-based), 0x40 = last part
    uint16_t name1[5];
    uint8_t  attr;               // always 0x0F
    uint8_t  type;
    uint8_t  checksum;           // of the 8.3 name it belongs to
    uint16_t name2[6];
    uint16_t zero;
    uint16_t name3[2];
} __attribute__((packed));

_Static_assert(sizeof(struct fat_dirent) == 32, "directory entry is 32 bytes");
_Static_assert(sizeof(struct fat_lfn) == 32, "LFN entry is 32 bytes");

#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN    0x02
#define ATTR_SYSTEM    0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_LFN       0x0F
#define ENTRY_FREE     0xE5      // first name byte of a deleted entry
#define CLUSTER_END    0x0FFFFFF8   // FAT value >= this: end of the chain

struct fat_fs {
    int drive;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t fat_sectors;
    uint32_t num_fats;
    uint32_t root_cluster;
    uint32_t first_data_sector;
    uint32_t cluster_bytes;
    uint32_t total_clusters;
    uint8_t fat_cache[ATA_SECTOR_SIZE];   // one FAT sector
    uint32_t fat_cache_sector;            // which one (0 = none yet)
};

static const struct vfs_ops fat_dir_ops;
static const struct vfs_ops fat_file_ops;

static bool read_cluster(struct fat_fs *fs, uint32_t cluster, void *buf) {
    uint32_t lba = fs->first_data_sector + (cluster - 2) * fs->sectors_per_cluster;
    return ata_read(fs->drive, lba, (uint8_t)fs->sectors_per_cluster, buf);
}

// Next cluster of the chain, or CLUSTER_END.
static uint32_t next_cluster(struct fat_fs *fs, uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t sector = fs->reserved_sectors + fat_offset / ATA_SECTOR_SIZE;
    if (sector != fs->fat_cache_sector) {
        if (!ata_read(fs->drive, sector, 1, fs->fat_cache)) {
            return CLUSTER_END;
        }
        fs->fat_cache_sector = sector;
    }
    uint32_t value;
    memcpy(&value, fs->fat_cache + fat_offset % ATA_SECTOR_SIZE, 4);
    value &= 0x0FFFFFFF;   // top 4 bits are reserved
    if (value < 2 || value >= fs->total_clusters + 2) {
        return CLUSTER_END;   // free/bad/end, or a broken chain
    }
    return value;
}

// Checksum of an 8.3 name, stored in its LFN entries.
static uint8_t short_name_checksum(const char name[11]) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + (uint8_t)name[i]);
    }
    return sum;
}

// "README  TXT" -> "readme.txt"
static void short_name_to_string(const char name[11], char *out) {
    int len = 0;
    for (int i = 0; i < 8 && name[i] != ' '; i++) {
        char c = name[i];
        out[len++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    if (name[8] != ' ') {
        out[len++] = '.';
        for (int i = 8; i < 11 && name[i] != ' '; i++) {
            char c = name[i];
            out[len++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
        }
    }
    out[len] = '\0';
}

// Put the 13 characters of one LFN entry at their place in "name".
static void lfn_collect(const struct fat_lfn *lfn, char *name) {
    int part = (lfn->order & 0x1F) - 1;   // 0-based
    if (part < 0 || part >= 4) {
        return;   // names longer than 52 characters are not supported
    }
    uint16_t chars[13];
    memcpy(chars, lfn->name1, sizeof(lfn->name1));
    memcpy(chars + 5, lfn->name2, sizeof(lfn->name2));
    memcpy(chars + 11, lfn->name3, sizeof(lfn->name3));
    for (int i = 0; i < 13; i++) {
        uint16_t c = chars[i];
        char ch = (c == 0 || c == 0xFFFF) ? '\0' : (c < 128 ? (char)c : '?');
        name[part * 13 + i] = ch;
    }
}

// Read a directory's entries from disk into child nodes (once).
static void load_dir(struct vfs_node *dir) {
    if (dir->children_loaded) {
        return;
    }
    dir->children_loaded = true;
    struct fat_fs *fs = dir->priv;
    uint8_t *buf = kmalloc(fs->cluster_bytes);
    if (buf == NULL) {
        return;
    }
    char long_name[VFS_NAME_MAX];
    int lfn_checksum = -1;   // -1: no long name collected

    for (uint32_t cluster = (uint32_t)dir->priv2; cluster != CLUSTER_END;
         cluster = next_cluster(fs, cluster)) {
        if (!read_cluster(fs, cluster, buf)) {
            break;
        }
        for (uint32_t off = 0; off < fs->cluster_bytes; off += 32) {
            struct fat_dirent *e = (struct fat_dirent *)(buf + off);
            if (e->name[0] == 0) {
                goto done;   // no more entries after this one
            }
            if ((uint8_t)e->name[0] == ENTRY_FREE) {
                lfn_checksum = -1;
                continue;
            }
            if (e->attr == ATTR_LFN) {
                struct fat_lfn *lfn = (struct fat_lfn *)e;
                if (lfn->order & 0x40) {   // first one we see = last part of the name
                    memset(long_name, 0, sizeof(long_name));
                    lfn_checksum = lfn->checksum;
                }
                lfn_collect(lfn, long_name);
                continue;
            }
            if (e->attr & ATTR_VOLUME_ID) {
                lfn_checksum = -1;
                continue;
            }
            char name[VFS_NAME_MAX];
            if (lfn_checksum == short_name_checksum(e->name) && long_name[0] != '\0') {
                memcpy(name, long_name, sizeof(name));
                name[VFS_NAME_MAX - 1] = '\0';
            } else {
                short_name_to_string(e->name, name);
            }
            lfn_checksum = -1;
            if ((name[0] == '.' && name[1] == '\0')
                || (name[0] == '.' && name[1] == '.' && name[2] == '\0')) {
                continue;   // the VFS handles . and .. itself
            }

            bool is_dir = (e->attr & ATTR_DIRECTORY) != 0;
            struct vfs_node *n = vfs_create_node(dir, name, is_dir ? VFS_DIR : VFS_FILE,
                                                 is_dir ? &fat_dir_ops : &fat_file_ops);
            if (n == NULL) {
                goto done;
            }
            n->priv = fs;
            n->priv2 = ((uint32_t)e->cluster_hi << 16) | e->cluster_lo;
            n->size = is_dir ? 0 : e->size;
        }
    }
done:
    kfree(buf);
}

static struct vfs_node *fat_lookup(struct vfs_node *dir, const char *name) {
    load_dir(dir);
    return vfs_memdir_ops.lookup(dir, name);
}

static struct vfs_node *fat_readdir(struct vfs_node *dir, uint64_t index) {
    load_dir(dir);
    return vfs_memdir_ops.readdir(dir, index);
}

static int64_t fat_read(struct vfs_node *node, uint64_t offset, void *buf, uint64_t len) {
    struct fat_fs *fs = node->priv;
    if (offset >= node->size) {
        return 0;
    }
    if (len > node->size - offset) {
        len = node->size - offset;
    }
    uint8_t *cluster_buf = kmalloc(fs->cluster_bytes);
    if (cluster_buf == NULL) {
        return -ENOMEM;
    }
    // Follow the chain to the cluster that holds "offset".
    uint32_t cluster = (uint32_t)node->priv2;
    for (uint64_t skip = offset / fs->cluster_bytes; skip > 0 && cluster != CLUSTER_END; skip--) {
        cluster = next_cluster(fs, cluster);
    }
    uint64_t done = 0;
    uint64_t in_cluster = offset % fs->cluster_bytes;
    while (done < len && cluster != CLUSTER_END) {
        if (!read_cluster(fs, cluster, cluster_buf)) {
            kfree(cluster_buf);
            return done > 0 ? (int64_t)done : -EINVAL;
        }
        uint64_t chunk = fs->cluster_bytes - in_cluster;
        if (chunk > len - done) {
            chunk = len - done;
        }
        memcpy((uint8_t *)buf + done, cluster_buf + in_cluster, chunk);
        done += chunk;
        in_cluster = 0;
        cluster = next_cluster(fs, cluster);
    }
    kfree(cluster_buf);
    return (int64_t)done;
}

static const struct vfs_ops fat_dir_ops = { .lookup = fat_lookup, .readdir = fat_readdir };
static const struct vfs_ops fat_file_ops = { .read = fat_read };

bool fat32_mount(int ata_drive, const char *path) {
    const struct ata_drive *d = ata_get_drive(ata_drive);
    if (d == NULL || !d->present) {
        return false;
    }
    uint8_t sector[ATA_SECTOR_SIZE];
    if (!ata_read(ata_drive, 0, 1, sector)) {
        kprintf("fat32: cannot read the boot sector\n");
        return false;
    }
    struct fat32_bpb *bpb = (struct fat32_bpb *)sector;
    if (sector[510] != 0x55 || sector[511] != 0xAA || memcmp(bpb->fs_type, "FAT32", 5) != 0
        || bpb->bytes_per_sector != ATA_SECTOR_SIZE || bpb->sectors_per_cluster == 0) {
        kprintf("fat32: drive %d has no FAT32 volume\n", ata_drive);
        return false;
    }

    struct fat_fs *fs = kzalloc(sizeof(*fs));
    if (fs == NULL) {
        return false;
    }
    fs->drive = ata_drive;
    fs->sectors_per_cluster = bpb->sectors_per_cluster;
    fs->reserved_sectors = bpb->reserved_sectors;
    fs->fat_sectors = bpb->fat_size32;
    fs->num_fats = bpb->num_fats;
    fs->root_cluster = bpb->root_cluster;
    fs->first_data_sector = fs->reserved_sectors + fs->num_fats * fs->fat_sectors;
    fs->cluster_bytes = fs->sectors_per_cluster * ATA_SECTOR_SIZE;
    fs->total_clusters = (bpb->total_sectors32 - fs->first_data_sector) / fs->sectors_per_cluster;

    struct vfs_node *mount = vfs_mkdir_p(path);
    if (mount == NULL) {
        kfree(fs);
        return false;
    }
    // The mount point becomes the volume's root directory.
    mount->ops = &fat_dir_ops;
    mount->priv = fs;
    mount->priv2 = fs->root_cluster;
    mount->children_loaded = false;

    char label[12];
    memcpy(label, bpb->volume_label, 11);
    label[11] = '\0';
    for (int i = 10; i >= 0 && label[i] == ' '; i--) {
        label[i] = '\0';   // labels are padded with spaces
    }
    kprintf("fat32: '%s' mounted at %s (%u clusters of %u bytes, data at sector %u)\n",
            label, path, fs->total_clusters, fs->cluster_bytes, fs->first_data_sector);
    return true;
}
