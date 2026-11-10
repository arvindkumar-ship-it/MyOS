#pragma once

// PS/2 keyboard (IRQ 1, data port 0x60). Turns scancodes (set 1, which the
// controller produces by default through "translation") into ASCII and
// pushes them into the input queue (drivers/input.h).

void keyboard_init(void);
