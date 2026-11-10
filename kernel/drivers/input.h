#pragma once

#include <stdbool.h>

// One queue of typed characters. The PS/2 keyboard and the serial port both
// push into it (from their IRQ handlers); readers take characters out.
// Serial input lets the headless tests and "-serial stdio" type too.

// Add a character (called from IRQ handlers).
void input_push(char c);

// Take a character if one is waiting. Never blocks.
bool input_try_getc(char *c);

// Wait until a character arrives (sleeps the CPU with hlt). Needs interrupts on.
char input_getc(void);

// Optional hook run after every pushed character (Stage 6 uses it to wake up
// a task waiting for input). Runs in IRQ context.
void input_set_push_callback(void (*callback)(void));
