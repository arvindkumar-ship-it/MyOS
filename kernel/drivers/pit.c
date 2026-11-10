#include "drivers/pit.h"

#include <stddef.h>
#include "arch/x86_64/io.h"
#include "arch/x86_64/irq.h"
#include "arch/x86_64/cpu.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_HZ  1193182

// Command byte: channel 0 (bits 6-7 = 00), access lobyte then hibyte (11),
// mode 2 "rate generator" (010), binary counting (0).
#define PIT_CMD_CH0_RATE 0x34

#define PIT_IRQ 0

static volatile uint64_t ticks;
static void (*tick_callback)(void);

static void pit_irq(struct interrupt_frame *frame) {
    (void)frame;
    ticks++;
    if (tick_callback != NULL) {
        tick_callback();
    }
}

void pit_init(void) {
    uint32_t divisor = PIT_BASE_HZ / PIT_HZ;   // 11931 -> 100.007 Hz

    outb(PIT_COMMAND, PIT_CMD_CH0_RATE);
    outb(PIT_CHANNEL0, divisor & 0xFF);         // low byte first
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);  // then high byte

    ticks = 0;
    irq_register(PIT_IRQ, pit_irq);
}

uint64_t pit_ticks(void) {
    return ticks;
}

uint64_t pit_uptime_ms(void) {
    return ticks * (1000 / PIT_HZ);
}

void pit_set_tick_callback(void (*callback)(void)) {
    tick_callback = callback;
}

void pit_sleep_ms(uint64_t ms) {
    // Round up to whole ticks, and add one: we may be in the middle of a tick now.
    uint64_t wait_ticks = (ms * PIT_HZ + 999) / 1000 + 1;
    uint64_t end = ticks + wait_ticks;
    while (ticks < end) {
        cpu_hlt();   // sleep until the next interrupt (timer or other)
    }
}
