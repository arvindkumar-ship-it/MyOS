#pragma once

#include <stdbool.h>

// COM1 serial port driver (16550 UART). In QEMU, "-serial stdio"
// connects COM1 to the terminal, so this is our first debug output.

// Set up COM1. Returns false if the port does not work (loopback test failed).
bool serial_init(void);

// Send one character. "\n" is sent as "\r\n" so terminals start a new line.
void serial_putc(char c);

// Send a NUL-terminated string.
void serial_write(const char *s);

// Turn on the "data received" interrupt (IRQ 4): typed characters go to the
// input queue (drivers/input.h). Call after irq_init().
void serial_enable_rx_irq(void);
