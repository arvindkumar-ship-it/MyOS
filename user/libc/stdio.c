#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

// Same formatter idea as the kernel's kprintf: it writes through a "struct out",
// either into a buffer (snprintf) or into a small buffer that is flushed with
// write() (printf).

struct out {
    void (*putc)(struct out *o, char c);
    char *buf;
    size_t size;
    size_t len;
    int fd;
};

static void buf_putc(struct out *o, char c) {
    if (o->len + 1 < o->size) {
        o->buf[o->len] = c;
    }
    o->len++;
}

#define FD_BUF 128

// Write what is left in the buffer. (A full buffer was already written by
// fd_putc, so only len % FD_BUF characters remain.)
static void fd_flush(struct out *o) {
    size_t n = o->len % FD_BUF;
    if (n > 0) {
        write(o->fd, o->buf, n);
    }
}

static void fd_putc(struct out *o, char c) {
    o->buf[o->len % FD_BUF] = c;
    o->len++;
    if (o->len % FD_BUF == 0) {
        write(o->fd, o->buf, FD_BUF);
    }
}

static void out_str(struct out *o, const char *s) {
    while (*s) {
        o->putc(o, *s++);
    }
}

static void print_unsigned(struct out *o, uint64_t v, unsigned base, bool upper, int width, char pad) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[20];
    int n = 0;
    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (v);
    for (int i = n; i < width; i++) {
        o->putc(o, pad);
    }
    while (n) {
        o->putc(o, tmp[--n]);
    }
}

static void format(struct out *o, const char *fmt, va_list ap) {
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            o->putc(o, *p);
            continue;
        }
        p++;
        char pad = ' ';
        bool left = false;
        for (;; p++) {
            if (*p == '0') {
                pad = '0';
            } else if (*p == '-') {
                left = true;
            } else {
                break;
            }
        }
        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p++ - '0');
        }
        bool is_long = false;
        while (*p == 'l') {
            is_long = true;
            p++;
        }
        switch (*p) {
        case 'd':
        case 'i': {
            int64_t v = is_long ? va_arg(ap, long) : va_arg(ap, int);
            uint64_t mag = v < 0 ? -(uint64_t)v : (uint64_t)v;
            if (v < 0) {
                o->putc(o, '-');
                width--;
            }
            print_unsigned(o, mag, 10, false, width, pad);
            break;
        }
        case 'u':
            print_unsigned(o, is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 10, false, width, pad);
            break;
        case 'x':
        case 'X':
            print_unsigned(o, is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned), 16, *p == 'X', width, pad);
            break;
        case 'p':
            out_str(o, "0x");
            print_unsigned(o, (uintptr_t)va_arg(ap, void *), 16, false, 16, '0');
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) {
                s = "(null)";
            }
            int len = (int)strlen(s);
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
            o->putc(o, (char)va_arg(ap, int));
            break;
        case '%':
            o->putc(o, '%');
            break;
        case '\0':
            return;
        default:
            o->putc(o, '%');
            o->putc(o, *p);
        }
    }
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    struct out o = { .putc = buf_putc, .buf = buf, .size = size };
    format(&o, fmt, ap);
    if (size) {
        buf[o.len < size ? o.len : size - 1] = '\0';
    }
    return (int)o.len;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

static int vdprintf_impl(int fd, const char *fmt, va_list ap) {
    char buf[FD_BUF];
    struct out o = { .putc = fd_putc, .buf = buf, .fd = fd };
    format(&o, fmt, ap);
    fd_flush(&o);
    return (int)o.len;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vdprintf_impl(STDOUT_FILENO, fmt, ap);
    va_end(ap);
    return n;
}

int dprintf(int fd, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vdprintf_impl(fd, fmt, ap);
    va_end(ap);
    return n;
}

int putchar(int c) {
    char ch = (char)c;
    write(STDOUT_FILENO, &ch, 1);
    return c;
}

int puts(const char *s) {
    write(STDOUT_FILENO, s, strlen(s));
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}
