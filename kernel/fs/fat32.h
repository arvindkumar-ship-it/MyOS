#pragma once

#include <stdbool.h>
#include "fs/vfs.h"

// FAT32 filesystem on an ATA disk, read only.
// Mounts the volume at "path" (for example "/disk"). The disk must hold a FAT32
// volume starting at sector 0 (no partition table), as "mformat -F" makes it.
bool fat32_mount(int ata_drive, const char *path);
