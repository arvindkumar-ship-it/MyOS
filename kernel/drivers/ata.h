#pragma once

#include <stdint.h>
#include <stdbool.h>

// ATA (IDE) hard disk driver, PIO mode: the CPU moves every 16-bit word
// itself through I/O port 0x1F0 (no DMA). Slow, but very simple.
// Only the primary bus: drive 0 = master, drive 1 = slave.
// Polling, no IRQ 14. Addresses are 28-bit LBA (sector numbers, 512 bytes each).

#define ATA_SECTOR_SIZE 512

struct ata_drive {
    bool present;
    uint32_t sectors;    // total number of sectors (LBA28)
    char model[41];
};

// Find the drives (IDENTIFY command). Prints what it finds.
void ata_init(void);

const struct ata_drive *ata_get_drive(int drive);

// Read/write "count" sectors (1-255) starting at "lba". buf: count * 512 bytes.
// Return false on error.
bool ata_read(int drive, uint32_t lba, uint8_t count, void *buf);
bool ata_write(int drive, uint32_t lba, uint8_t count, const void *buf);
