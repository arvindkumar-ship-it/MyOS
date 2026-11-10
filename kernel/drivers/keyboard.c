#include "drivers/keyboard.h"

#include <stdint.h>
#include <stdbool.h>
#include "arch/x86_64/io.h"
#include "arch/x86_64/irq.h"
#include "drivers/input.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_STATUS_OUTPUT_FULL 0x01   // a byte is waiting in port 0x60

#define KEYBOARD_IRQ 1

// Scancode set 1: each key sends one byte when pressed ("make code") and the
// same byte with bit 7 set when released ("break code"). Some keys send 0xE0 first.
#define SC_RELEASED     0x80
#define SC_EXTENDED     0xE0
#define SC_LEFT_SHIFT   0x2A
#define SC_RIGHT_SHIFT  0x36
#define SC_CTRL         0x1D
#define SC_CAPS_LOCK    0x3A

// US layout, index = make code. 0 = no character for that key.
static const char normal_map[128] = {
    0,    27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',  // 0x00-0x0E
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',      // 0x0F-0x1C
    0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',           // 0x1D-0x29
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,             // 0x2A-0x36
    '*',  0,   ' ',                                                               // 0x37-0x39
};

static const char shift_map[128] = {
    0,    27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*',  0,   ' ',
};

static bool shift_down, ctrl_down, caps_lock, extended;

static void keyboard_irq(struct interrupt_frame *frame) {
    (void)frame;
    uint8_t sc = inb(PS2_DATA);

    if (sc == SC_EXTENDED) {
        extended = true;      // the real code comes in the next IRQ
        return;
    }
    bool released = (sc & SC_RELEASED) != 0;
    uint8_t code = sc & ~SC_RELEASED;

    if (extended) {
        // 0xE0 keys: right ctrl (0x1D), arrows, keypad Enter... We only care about ctrl.
        extended = false;
        if (code == SC_CTRL) {
            ctrl_down = !released;
        }
        return;
    }

    switch (code) {
    case SC_LEFT_SHIFT:
    case SC_RIGHT_SHIFT:
        shift_down = !released;
        return;
    case SC_CTRL:
        ctrl_down = !released;
        return;
    case SC_CAPS_LOCK:
        if (!released) {
            caps_lock = !caps_lock;
        }
        return;
    }

    if (released) {
        return;   // characters are sent on press only
    }

    char c = shift_down ? shift_map[code] : normal_map[code];
    if (c == 0) {
        return;
    }
    // Caps lock flips the case of letters only.
    if (caps_lock && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
        c ^= 0x20;
    }
    // Ctrl+letter gives control codes 1-26 (Ctrl+C = 3).
    if (ctrl_down && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
        c = (char)((c & 0x1F));
    }
    input_push(c);
}

void keyboard_init(void) {
    // Throw away any bytes left in the controller from before we took over.
    while (inb(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL) {
        inb(PS2_DATA);
    }
    irq_register(KEYBOARD_IRQ, keyboard_irq);
}
