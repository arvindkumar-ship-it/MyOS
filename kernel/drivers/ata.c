#include "drivers/ata.h"

#include "arch/x86_64/io.h"
#include "arch/x86_64/cpu.h"
#include "lib/kprintf.h"

// Primary ATA bus registers.
#define ATA_DATA        0x1F0   // 16-bit data port
#define ATA_ERROR       0x1F1
#define ATA_SECCOUNT    0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE_HEAD  0x1F6   // bit 6: LBA mode, bit 4: drive (0 master, 1 slave), bits 0-3: LBA 24-27
#define ATA_STATUS      0x1F7   // read: status
#define ATA_COMMAND     0x1F7   // write: command
#define ATA_ALT_STATUS  0x3F6   // read: status without side effects
#define ATA_CONTROL     0x3F6   // write: bit 1 nIEN = no interrupts

#define STATUS_ERR  0x01
#define STATUS_DRQ  0x08        // data ready to transfer
#define STATUS_DF   0x20        // drive fault
#define STATUS_BSY  0x80        // busy: other bits are not valid yet

#define CMD_READ_SECTORS  0x20
#define CMD_WRITE_SECTORS 0x30
#define CMD_CACHE_FLUSH   0xE7
#define CMD_IDENTIFY      0xEC

#define CONTROL_NIEN 0x02

static struct ata_drive drives[2];

// Each status read takes ~100 ns; 4 reads = the 400 ns a drive needs after
// a drive select or command before its status is valid.
static void delay_400ns(void) {
    for (int i = 0; i < 4; i++) {
        inb(ATA_ALT_STATUS);
    }
}

// Wait until not busy. Returns the status, or 0xFF on timeout.
static uint8_t wait_not_busy(void) {
    for (uint32_t i = 0; i < 1000000; i++) {
        uint8_t status = inb(ATA_STATUS);
        if (!(status & STATUS_BSY)) {
            return status;
        }
    }
    return 0xFF;
}

// Wait until the drive has data for us (or wants data), or reports an error.
static bool wait_drq(void) {
    for (uint32_t i = 0; i < 1000000; i++) {
        uint8_t status = inb(ATA_STATUS);
        if (status & STATUS_BSY) {
            continue;
        }
        if (status & (STATUS_ERR | STATUS_DF)) {
            return false;
        }
        if (status & STATUS_DRQ) {
            return true;
        }
    }
    return false;
}

static void identify(int drive) {
    struct ata_drive *d = &drives[drive];
    d->present = false;

    outb(ATA_DRIVE_HEAD, drive ? 0xB0 : 0xA0);
    delay_400ns();
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);
    outb(ATA_COMMAND, CMD_IDENTIFY);
    delay_400ns();

    if (inb(ATA_STATUS) == 0) {
        return;   // status 0: no drive there
    }
    if (wait_not_busy() == 0xFF) {
        return;
    }
    // ATAPI (CD-ROM) and SATA drives put a signature in LBA mid/hi: not a plain ATA disk.
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HI) != 0) {
        return;
    }
    if (!wait_drq()) {
        return;
    }

    // IDENTIFY returns 256 16-bit words of information.
    uint16_t info[256];
    for (int i = 0; i < 256; i++) {
        info[i] = inw(ATA_DATA);
    }
    d->present = true;
    d->sectors = (uint32_t)info[60] | ((uint32_t)info[61] << 16);   // LBA28 sector count
    // Model name: words 27-46, two characters per word, high byte first.
    for (int i = 0; i < 20; i++) {
        d->model[i * 2] = (char)(info[27 + i] >> 8);
        d->model[i * 2 + 1] = (char)(info[27 + i] & 0xFF);
    }
    d->model[40] = '\0';
    for (int i = 39; i >= 0 && d->model[i] == ' '; i--) {
        d->model[i] = '\0';
    }
}

void ata_init(void) {
    outb(ATA_CONTROL, CONTROL_NIEN);   // we poll; no IRQ 14 please
    for (int drive = 0; drive < 2; drive++) {
        identify(drive);
        if (drives[drive].present) {
            kprintf("ata: drive %d: '%s', %u sectors (%u MiB)\n", drive, drives[drive].model,
                    drives[drive].sectors, drives[drive].sectors / 2048);
        }
    }
}

const struct ata_drive *ata_get_drive(int drive) {
    return (drive == 0 || drive == 1) ? &drives[drive] : NULL;
}

// Select drive + LBA and send a command.
static bool start_command(int drive, uint32_t lba, uint8_t count, uint8_t command) {
    if ((drive != 0 && drive != 1) || !drives[drive].present || count == 0
        || lba + count > drives[drive].sectors) {
        return false;
    }
    if (wait_not_busy() == 0xFF) {
        return false;
    }
    outb(ATA_DRIVE_HEAD, (uint8_t)(0xE0 | (drive << 4) | ((lba >> 24) & 0x0F)));
    delay_400ns();
    outb(ATA_SECCOUNT, count);
    outb(ATA_LBA_LO, (uint8_t)lba);
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND, command);
    return true;
}

bool ata_read(int drive, uint32_t lba, uint8_t count, void *buf) {
    // One request at a time: interrupts off so no other task uses the bus meanwhile.
    bool was_enabled = cpu_irq_save();
    bool ok = start_command(drive, lba, count, CMD_READ_SECTORS);
    uint16_t *words = buf;
    for (int s = 0; ok && s < count; s++) {
        delay_400ns();
        if (!wait_drq()) {
            ok = false;
            break;
        }
        for (int i = 0; i < 256; i++) {
            *words++ = inw(ATA_DATA);
        }
    }
    cpu_irq_restore(was_enabled);
    return ok;
}

bool ata_write(int drive, uint32_t lba, uint8_t count, const void *buf) {
    bool was_enabled = cpu_irq_save();
    bool ok = start_command(drive, lba, count, CMD_WRITE_SECTORS);
    const uint16_t *words = buf;
    for (int s = 0; ok && s < count; s++) {
        delay_400ns();
        if (!wait_drq()) {
            ok = false;
            break;
        }
        for (int i = 0; i < 256; i++) {
            outw(ATA_DATA, *words++);
        }
    }
    if (ok) {
        // Make the drive write its cache to the medium.
        outb(ATA_COMMAND, CMD_CACHE_FLUSH);
        ok = wait_not_busy() != 0xFF;
    }
    cpu_irq_restore(was_enabled);
    return ok;
}
