#pragma once

#include <stdint.h>
#include <stdbool.h>

// Interrupt Descriptor Table: 256 entries. Entry N says which function the
// CPU runs for interrupt/exception vector N, and with which privileges.
//   0-31   CPU exceptions (divide error, page fault, ...)
//   32-47  hardware IRQs from the PIC (after remapping, Stage 4)
//   0x80   system call from user mode (Stage 7)

#define IDT_ENTRIES 256

// Gate types (low 4 bits of the attribute byte).
#define IDT_TYPE_INTERRUPT 0xE   // interrupts are disabled (IF=0) while the handler runs
#define IDT_TYPE_TRAP      0xF   // IF is left unchanged

// Fill all 256 entries with the stubs from isr.asm and load the IDT.
void idt_init(void);

// Change one entry. dpl = lowest ring allowed to raise it with "int N"
// (0 = only kernel, 3 = user too). ist = IST slot (0 = none).
void idt_set_gate(uint8_t vector, uint8_t ist, uint8_t dpl, uint8_t type);

// Mark an entry present/not present (used by the double fault test).
void idt_set_present(uint8_t vector, bool present);
