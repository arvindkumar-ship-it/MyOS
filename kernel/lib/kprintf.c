#include "lib/kprintf.h"

#include <stdint.h>
#include <stdbool.h>
#include "drivers/serial.h"
#include "drivers/console.h"
#include "arch/x86_64/cpu.h"

// The formatter writes through a "struct out", so the same code can print
// to the screen/serial (kprintf) or into a buffer (ksnprintf).
struct out {
    void (*putc)(struct out *o, char c);
    char *buf;      // ksnprintf only
    size_t size;
    size_t len;     // characters produced so far (also counts ones that did not fit)
};

// Send one character everywhere we can show text.
static void screen_putc(struct out *o, char c) {
    o->len++;
    serial_putc(c);
    if (console_is_ready()) {
        console_putc(c);
    }
}

static void buffer_putc(struct out *o, char c) {
    // Keep one byte free for the final '\0'.
    if (o->len + 1 < o->size) {
        o->buf[o->len] = c;
    }
    o->len++;
}

static void out_str(struct out *o, const char *s) {
    while (*s) {
        o->putc(o, *s++);
    }
}

// Print an unsigned number in base 10 or 16.
// width: minimum number of characters; pad: '0' or ' ' to fill the gap.
static void print_unsigned(struct out *o, uint64_t value, unsigned base, bool upper,
                           int width, char pad) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[20];   // 2^64 - 1 has 20 decimal digits (and 16 hex digits)
    int len = 0;

    // Digits come out last-first (value % base is the lowest digit),
    // so we store them backwards and print the buffer in reverse.
    do {
        buf[len++] = digits[value % base];
        value /= base;
    } while (value != 0);

    for (int i = len; i < width; i++) {
        o->putc(o, pad);
    }
    while (len > 0) {
        o->putc(o, buf[--len]);
    }
}

static void print_signed(struct out *o, int64_t value, int width, char pad) {
    uint64_t magnitude;
    if (value < 0) {
        // Do not write -value: for INT64_MIN that overflows (undefined).
        // Converting to unsigned first and negating there is well defined.
        magnitude = -(uint64_t)value;
        if (pad == '0') {
            // "-0042", not "00-42": sign goes before the zeros.
            o->putc(o, '-');
            print_unsigned(o, magnitude, 10, false, width - 1, pad);
            return;
        }
        // "  -42": spaces, then sign, then digits. Count digits for the padding.
        int digits = 0;
        for (uint64_t v = magnitude; v != 0; v /= 10) {
            digits++;
        }
        for (int i = digits + 1; i < width; i++) {
            o->putc(o, ' ');
        }
        o->putc(o, '-');
        print_unsigned(o, magnitude, 10, false, 0, ' ');
        return;
    }
    print_unsigned(o, (uint64_t)value, 10, false, width, pad);
}

static void format(struct out *o, const char *fmt, va_list args) {
    for (const char *p = fmt; *p != '\0'; p++) {
        if (*p != '%') {
            o->putc(o, *p);
            continue;
        }
        p++;   // skip '%'

        // Flags: '0' pads with zeros, '-' aligns left (pads on the right).
        char pad = ' ';
        bool left = false;
        for (;;) {
            if (*p == '0') {
                pad = '0';
                p++;
            } else if (*p == '-') {
                left = true;
                p++;
            } else {
                break;
            }
        }

        // Width: decimal number.
        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        // Length: l or ll means the argument is 64-bit.
        // (On x86_64, long and long long are both 64-bit.)
        bool is_long = false;
        while (*p == 'l') {
            is_long = true;
            p++;
        }

        switch (*p) {
        case 'd':
        case 'i': {
            // va_arg must use the exact type that was passed.
            int64_t v = is_long ? va_arg(args, long) : va_arg(args, int);
            print_signed(o, v, width, pad);
            break;
        }
        case 'u': {
            uint64_t v = is_long ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
            print_unsigned(o, v, 10, false, width, pad);
            break;
        }
        case 'x':
        case 'X': {
            uint64_t v = is_long ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
            print_unsigned(o, v, 16, *p == 'X', width, pad);
            break;
        }
        case 'p': {
            // Pointers: always 0x + 16 hex digits, so addresses line up.
            uintptr_t v = (uintptr_t)va_arg(args, void *);
            out_str(o, "0x");
            print_unsigned(o, v, 16, false, 16, '0');
            break;
        }
        case 's': {
            const char *s = va_arg(args, const char *);
            if (s == NULL) {
                s = "(null)";
            }
            int len = 0;
            while (s[len] != '\0') {
                len++;
            }
            if (!left) {
                for (int i = len; i < width; i++) {
                    o->putc(o, ' ');
                }
            }
            out_str(o, s);
            if (left) {
                for (int i = len; i < width; i++) {
                    o->putc(o, ' ');
                }
            }
            break;
        }
        case 'c':
            // char is promoted to int when passed through "...".
            o->putc(o, (char)va_arg(args, int));
            break;
        case '%':
            o->putc(o, '%');
            break;
        case '\0':
            // Format string ended right after '%'. Stop, do not read past the end.
            return;
        default:
            // Unknown specifier: print it as-is so the mistake is visible.
            o->putc(o, '%');
            o->putc(o, *p);
            break;
        }
    }
}

void kvprintf(const char *fmt, va_list args) {
    struct out o = { .putc = screen_putc };
    // Interrupts off while printing, so a message from an interrupt handler
    // or another task (Stage 6) cannot land in the middle of this one and
    // the console cursor is never updated by two printers at once.
    bool was_enabled = cpu_irq_save();
    format(&o, fmt, args);
    cpu_irq_restore(was_enabled);
}

void kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
}

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list args) {
    struct out o = { .putc = buffer_putc, .buf = buf, .size = size, .len = 0 };
    format(&o, fmt, args);
    if (size > 0) {
        buf[o.len < size ? o.len : size - 1] = '\0';
    }
    return (int)o.len;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = kvsnprintf(buf, size, fmt, args);
    va_end(args);
    return n;
}
