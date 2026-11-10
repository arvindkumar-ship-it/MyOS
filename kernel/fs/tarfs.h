#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "fs/vfs.h"

// tarfs: a read-only filesystem from a USTAR .tar archive in memory (the
// initrd, loaded by Limine as a module). Every file and directory in the
// archive is added under "dir"; file contents are read straight from the archive.
// Returns the number of entries added, or -1 if the archive is broken.
int tarfs_mount(struct vfs_node *dir, const void *virt_archive, uint64_t size);
