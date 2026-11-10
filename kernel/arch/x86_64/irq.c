#include "arch/x86_64/irq.h"

#include <stddef.h>
#include "drivers/pic.h"
#include "lib/kprintf.h"
#include "arch/x86_64/cpu.h"

// IA32_APIC_BASE MSR: bit 11 = Local APIC globally enabled.
#define MSR_APIC_BASE        0x1B
#define APIC_BASE_ENABLE     (1ul << 11)

static irq_handler_t irq_handlers[PIC_IRQ_COUNT];
static volatile uint64_t irq_counts[PIC_IRQ_COUNT];

// Runs for every vector 32-47.
static void irq_dispatch(struct interrupt_frame *frame) {
    uint8_t irq = (uint8_t)(frame->vector - PIC_IRQ_BASE);

    if (pic_is_spurious(irq)) {
        return;   // no EOI for a spurious IRQ (pic_is_spurious handled the master case)
    }
    irq_counts[irq]++;

    // EOI first, then the handler. The handler may switch to another task
    // (Stage 6) and only come back much later; the PIC must not wait for that.
    // Another IRQ cannot interrupt us yet: interrupt gates keep IF = 0
    // until iretq.
    pic_send_eoi(irq);

    if (irq_handlers[irq] != NULL) {
        irq_handlers[irq](frame);
    }
}

// Every modern x86 CPU has a Local APIC between the 8259 PIC and the core:
// PIC interrupts enter it on pin LINT0 ("ExtINT"). Limine leaves the LAPIC
// enabled with LINT0 *masked*, so PIC IRQs never reach the CPU (QEMU's
// "info lapic" shows LVT0 ... masked ExtINT). We use the old 8259 for now, so
// we switch the LAPIC off completely: then LINT0 works as the plain INTR pin
// again (Intel SDM Vol. 3, "Local APIC State After It Has Been Disabled").
static void lapic_disable(void) {
    uint64_t base = cpu_rdmsr(MSR_APIC_BASE);
    cpu_wrmsr(MSR_APIC_BASE, base & ~APIC_BASE_ENABLE);
}

void irq_init(void) {
    lapic_disable();
    pic_init();
    for (uint8_t irq = 0; irq < PIC_IRQ_COUNT; irq++) {
        interrupt_register(PIC_IRQ_BASE + irq, irq_dispatch);
    }
}

void irq_register(uint8_t irq, irq_handler_t handler) {
    irq_handlers[irq] = handler;
    pic_unmask(irq);
}

uint64_t irq_count(uint8_t irq) {
    return irq_counts[irq];
}
