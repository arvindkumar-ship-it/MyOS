#include "drivers/serial.h"

#include <stdint.h>
#include "arch/x86_64/io.h"
#include "arch/x86_64/irq.h"
#include "drivers/input.h"

// I/O port base address of COM1.
#define COM1 0x3F8

// UART registers are at offsets from the base port.
#define REG_DATA        0   // read: received byte, write: byte to send
#define REG_INT_ENABLE  1   // which UART interrupts are on
#define REG_DIVISOR_LO  0   // when DLAB = 1: baud divisor, low byte
#define REG_DIVISOR_HI  1   // when DLAB = 1: baud divisor, high byte
#define REG_FIFO_CTRL   2
#define REG_LINE_CTRL   3   // data bits, parity, stop bits, DLAB
#define REG_MODEM_CTRL  4
#define REG_LINE_STATUS 5

#define LINE_STATUS_TX_EMPTY 0x20   // bit 5: ready to accept a new byte
#define LINE_STATUS_DATA_READY 0x01 // bit 0: a received byte is waiting
#define INT_ENABLE_RX 0x01          // interrupt when a byte arrives

#define COM1_IRQ 4

static bool serial_ok = false;

bool serial_init(void) {
    outb(COM1 + REG_INT_ENABLE, 0x00);   // no UART interrupts, we poll

    // Baud rate: the UART clock is 115200 Hz, divided by "divisor".
    // DLAB (bit 7 of line control) switches registers 0/1 to the divisor.
    outb(COM1 + REG_LINE_CTRL, 0x80);    // DLAB = 1
    outb(COM1 + REG_DIVISOR_LO, 3);      // 115200 / 3 = 38400 baud
    outb(COM1 + REG_DIVISOR_HI, 0);
    outb(COM1 + REG_LINE_CTRL, 0x03);    // DLAB = 0, 8 data bits, no parity, 1 stop bit (8N1)

    outb(COM1 + REG_FIFO_CTRL, 0xC7);    // enable FIFO, clear it, 14-byte threshold

    // Self test: in loopback mode, what we send comes straight back.
    outb(COM1 + REG_MODEM_CTRL, 0x1E);   // loopback on
    outb(COM1 + REG_DATA, 0xAE);
    if (inb(COM1 + REG_DATA) != 0xAE) {
        return false;                    // no working UART here
    }

    outb(COM1 + REG_MODEM_CTRL, 0x0F);   // normal mode (DTR, RTS, OUT1, OUT2 on)
    serial_ok = true;
    return true;
}

void serial_putc(char c) {
    if (!serial_ok) {
        return;   // without this, the wait below could spin forever
    }
    if (c == '\n') {
        serial_putc('\r');
    }
    // Busy-wait until the UART can take another byte.
    while ((inb(COM1 + REG_LINE_STATUS) & LINE_STATUS_TX_EMPTY) == 0) {
    }
    outb(COM1 + REG_DATA, (uint8_t)c);
}

void serial_write(const char *s) {
    while (*s) {
        serial_putc(*s++);
    }
}

static void serial_irq(struct interrupt_frame *frame) {
    (void)frame;
    // Read every byte the FIFO holds (several may have arrived).
    while (inb(COM1 + REG_LINE_STATUS) & LINE_STATUS_DATA_READY) {
        char c = (char)inb(COM1 + REG_DATA);
        // Terminals send Enter as '\r' and Backspace as DEL (0x7F).
        if (c == '\r') {
            c = '\n';
        } else if (c == 0x7F) {
            c = '\b';
        }
        input_push(c);
    }
}

void serial_enable_rx_irq(void) {
    if (!serial_ok) {
        return;
    }
    irq_register(COM1_IRQ, serial_irq);
    // OUT2 (bit 3 of modem control, set in serial_init) must be on: on PCs it
    // connects the UART interrupt line to the PIC.
    outb(COM1 + REG_INT_ENABLE, INT_ENABLE_RX);
}
