#pragma once

#include <stdint.h>
#include <stdbool.h>

// Legacy 8259 PIC (Programmable Interrupt Controller): two chips, 8 IRQ lines each.
//   master: IRQ 0-7   (0 = PIT timer, 1 = keyboard, 2 = slave cascade, 4 = COM1)
//   slave:  IRQ 8-15  (connected to IRQ 2 of the master)
//
// After reset the master sends IRQ 0-7 as vectors 8-15, which are CPU
// exception vectors (8 = double fault!). So we "remap" them to 32-47.

#define PIC_IRQ_BASE 32   // vector of IRQ 0
#define PIC_IRQ_COUNT 16

// Remap to vectors 32-47 and mask every IRQ.
void pic_init(void);

void pic_unmask(uint8_t irq);
void pic_mask(uint8_t irq);

// "End Of Interrupt": tell the PIC we handled the IRQ, so it can send the next one.
void pic_send_eoi(uint8_t irq);

// Spurious IRQ 7/15: the PIC raised the line but no device really asked.
// Returns true if this IRQ was spurious (then it must not get a normal EOI).
bool pic_is_spurious(uint8_t irq);
