#include <string.h>
#include <stdint.h>
#include "syscall_nr.h"

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == '\0') {
            return (unsigned char)a[i] - (unsigned char)b[i];
        }
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) {
    }
    return dst;
}

char *strchr(const char *s, int c) {
    for (; *s; s++) {
        if (*s == (char)c) {
            return (char *)s;
        }
    }
    return c == 0 ? (char *)s : NULL;
}

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    uint8_t *d = dst;
    for (size_t i = 0; i < n; i++) {
        d[i] = (uint8_t)c;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *p = a, *q = b;
    for (size_t i = 0; i < n; i++) {
        if (p[i] != q[i]) {
            return p[i] < q[i] ? -1 : 1;
        }
    }
    return 0;
}

const char *strerror(int err) {
    switch (err) {
    case ENOENT:  return "no such file or directory";
    case E2BIG:   return "argument list too long";
    case ENOEXEC: return "not an executable";
    case EBADF:   return "bad file descriptor";
    case ECHILD:  return "no such child";
    case ENOMEM:  return "out of memory";
    case EFAULT:  return "bad address";
    case ENOTDIR: return "not a directory";
    case EISDIR:  return "is a directory";
    case EINVAL:  return "invalid argument";
    case EMFILE:  return "too many open files";
    case EROFS:   return "read-only file system";
    case ENAMETOOLONG: return "name too long";
    case ENOSYS:  return "not implemented";
    default:      return "error";
    }
}
