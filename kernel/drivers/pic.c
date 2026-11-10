#include "drivers/pic.h"

#include "arch/x86_64/io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_INIT 0x10   // start initialization
#define ICW1_ICW4 0x01   // ICW4 will follow
#define ICW4_8086 0x01   // 8086/88 mode (not old 8080 mode)

#define CMD_EOI      0x20
#define CMD_READ_ISR 0x0B   // OCW3: next read of the command port returns the ISR

#define CASCADE_IRQ 2

void pic_init(void) {
    // Initialization is a fixed sequence of 4 "Initialization Command Words".
    // io_wait() gives the old chips time between writes.
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);  io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);  io_wait();

    outb(PIC1_DATA, PIC_IRQ_BASE);          io_wait();   // ICW2: master vector offset (32)
    outb(PIC2_DATA, PIC_IRQ_BASE + 8);      io_wait();   // ICW2: slave vector offset (40)

    outb(PIC1_DATA, 1 << CASCADE_IRQ);      io_wait();   // ICW3: slave is on master IRQ 2 (bit mask)
    outb(PIC2_DATA, CASCADE_IRQ);           io_wait();   // ICW3: slave's cascade identity (number)

    outb(PIC1_DATA, ICW4_8086);             io_wait();
    outb(PIC2_DATA, ICW4_8086);             io_wait();

    // Mask (disable) every line; drivers unmask the IRQ they handle.
    // The cascade line stays open so slave IRQs can get through once unmasked.
    outb(PIC1_DATA, (uint8_t)~(1 << CASCADE_IRQ));
    outb(PIC2_DATA, 0xFF);
}

void pic_unmask(uint8_t irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = irq & 7;
    outb(port, inb(port) & (uint8_t)~(1 << bit));
}

void pic_mask(uint8_t irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = irq & 7;
    outb(port, inb(port) | (uint8_t)(1 << bit));
}

void pic_send_eoi(uint8_t irq) {
    // A slave IRQ went through both chips, so both need an EOI.
    if (irq >= 8) {
        outb(PIC2_CMD, CMD_EOI);
    }
    outb(PIC1_CMD, CMD_EOI);
}

static uint8_t read_isr(uint16_t cmd_port) {
    outb(cmd_port, CMD_READ_ISR);
    return inb(cmd_port);
}

bool pic_is_spurious(uint8_t irq) {
    // Only the lowest-priority line of each chip (7 and 15) can be spurious.
    // If the "In-Service Register" bit is not set, nothing is really in service.
    if (irq == 7) {
        return (read_isr(PIC1_CMD) & 0x80) == 0;
    }
    if (irq == 15) {
        if ((read_isr(PIC2_CMD) & 0x80) == 0) {
            // The master did see a real signal on the cascade line: it needs its EOI.
            outb(PIC1_CMD, CMD_EOI);
            return true;
        }
    }
    return false;
}
