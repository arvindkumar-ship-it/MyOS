#include "lib/kprintf.h"

#include <stdint.h>
#include <stdbool.h>
#include "drivers/serial.h"
#include "drivers/console.h"

// Send one character everywhere we can show text.
static void kputc(char c) {
    serial_putc(c);
    if (console_is_ready()) {
        console_putc(c);
    }
}

static void kputs(const char *s) {
    while (*s) {
        kputc(*s++);
    }
}

// Print an unsigned number in base 10 or 16.
// width: minimum number of characters; pad: '0' or ' ' to fill the gap.
static void print_unsigned(uint64_t value, unsigned base, bool upper,
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
        kputc(pad);
    }
    while (len > 0) {
        kputc(buf[--len]);
    }
}

static void print_signed(int64_t value, int width, char pad) {
    uint64_t magnitude;
    if (value < 0) {
        // Do not write -value: for INT64_MIN that overflows (undefined).
        // Converting to unsigned first and negating there is well defined.
        magnitude = -(uint64_t)value;
        if (pad == '0') {
            // "-0042", not "00-42": sign goes before the zeros.
            kputc('-');
            print_unsigned(magnitude, 10, false, width - 1, pad);
            return;
        }
        // "  -42": spaces, then sign, then digits. Count digits for the padding.
        int digits = 0;
        for (uint64_t v = magnitude; v != 0; v /= 10) {
            digits++;
        }
        for (int i = digits + 1; i < width; i++) {
            kputc(' ');
        }
        kputc('-');
        print_unsigned(magnitude, 10, false, 0, ' ');
        return;
    }
    print_unsigned((uint64_t)value, 10, false, width, pad);
}

void kvprintf(const char *fmt, va_list args) {
    for (const char *p = fmt; *p != '\0'; p++) {
        if (*p != '%') {
            kputc(*p);
            continue;
        }
        p++;   // skip '%'

        // Flag: leading 0 means pad with zeros.
        char pad = ' ';
        if (*p == '0') {
            pad = '0';
            p++;
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
            print_signed(v, width, pad);
            break;
        }
        case 'u': {
            uint64_t v = is_long ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
            print_unsigned(v, 10, false, width, pad);
            break;
        }
        case 'x':
        case 'X': {
            uint64_t v = is_long ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
            print_unsigned(v, 16, *p == 'X', width, pad);
            break;
        }
        case 'p': {
            // Pointers: always 0x + 16 hex digits, so addresses line up.
            uintptr_t v = (uintptr_t)va_arg(args, void *);
            kputs("0x");
            print_unsigned(v, 16, false, 16, '0');
            break;
        }
        case 's': {
            const char *s = va_arg(args, const char *);
            kputs(s ? s : "(null)");
            break;
        }
        case 'c':
            // char is promoted to int when passed through "...".
            kputc((char)va_arg(args, int));
            break;
        case '%':
            kputc('%');
            break;
        case '\0':
            // Format string ended right after '%'. Stop, do not read past the end.
            return;
        default:
            // Unknown specifier: print it as-is so the mistake is visible.
            kputc('%');
            kputc(*p);
            break;
        }
    }
}

void kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
}
