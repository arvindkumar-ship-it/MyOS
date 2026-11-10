#pragma once

#include <stdint.h>
#include "arch/x86_64/interrupts.h"

// Hardware interrupts (IRQ 0-15 from the PIC, vectors 32-47).

typedef void (*irq_handler_t)(struct interrupt_frame *frame);

// Remap the PIC and route vectors 32-47 to irq handlers. Interrupts stay off.
void irq_init(void);

// Install a handler for an IRQ line and unmask it in the PIC.
void irq_register(uint8_t irq, irq_handler_t handler);

// Number of times each IRQ fired (for debugging and tests).
uint64_t irq_count(uint8_t irq);
