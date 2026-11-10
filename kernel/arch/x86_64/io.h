#pragma once

#include <stdint.h>

// x86 has a separate "I/O port" address space (0x0000 - 0xFFFF), next to
// normal memory. Old devices like the serial port, PIC, PIT and PS/2
// keyboard are reached through it with the special "in" and "out"
// instructions. C has no syntax for these, so we use inline assembly.

// Write one byte to an I/O port.
// "a"(value): put value in AL.  "Nd"(port): port in DX (or an 8-bit constant).
static inline void outb(uint16_t port, uint8_t value) {
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

// Read one byte from an I/O port.
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    asm volatile ("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}
