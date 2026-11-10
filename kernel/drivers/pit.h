#pragma once

#include <stdint.h>

// 8253/8254 PIT (Programmable Interval Timer). Channel 0 is wired to IRQ 0.
// Its input clock is 1.193182 MHz; it counts down from a "divisor" and fires
// an IRQ each time it reaches 0, so the IRQ rate is 1193182 / divisor Hz.

#define PIT_HZ 100   // ticks per second (one tick = 10 ms)

// Program channel 0 for PIT_HZ and install the IRQ 0 handler.
void pit_init(void);

// Ticks since pit_init().
uint64_t pit_ticks(void);

// Milliseconds since pit_init() (resolution: one tick = 10 ms).
uint64_t pit_uptime_ms(void);

// Extra function the timer IRQ calls on every tick (the scheduler, Stage 6).
void pit_set_tick_callback(void (*callback)(void));

// Wait at least "ms" milliseconds, sleeping the CPU with hlt between ticks.
// Needs interrupts enabled.
void pit_sleep_ms(uint64_t ms);
