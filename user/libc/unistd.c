#include <unistd.h>
#include <stdlib.h>
#include "syscall.h"

int64_t write(int fd, const void *buf, size_t len) {
    return syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, len);
}

int64_t read(int fd, void *buf, size_t len) {
    return syscall3(SYS_READ, (uint64_t)fd, (uint64_t)buf, len);
}

int open(const char *path, int flags) {
    return (int)syscall2(SYS_OPEN, (uint64_t)path, (uint64_t)flags);
}

int close(int fd) {
    return (int)syscall1(SYS_CLOSE, (uint64_t)fd);
}

int getpid(void) {
    return (int)syscall0(SYS_GETPID);
}

int sleep_ms(uint64_t ms) {
    return (int)syscall1(SYS_SLEEP, ms);
}

uint64_t uptime_ms(void) {
    return (uint64_t)syscall0(SYS_UPTIME);
}

int spawn(const char *path, const char *const *argv) {
    return (int)syscall2(SYS_SPAWN, (uint64_t)path, (uint64_t)argv);
}

int wait(int pid, int *status) {
    return (int)syscall2(SYS_WAIT, (uint64_t)pid, (uint64_t)status);
}

int readdir(const char *path, int index, struct myos_dirent *entry) {
    return (int)syscall3(SYS_READDIR, (uint64_t)path, (uint64_t)index, (uint64_t)entry);
}

int stat(const char *path, struct myos_stat *st) {
    return (int)syscall2(SYS_STAT, (uint64_t)path, (uint64_t)st);
}

int ps(struct myos_procinfo *list, int max) {
    return (int)syscall2(SYS_PS, (uint64_t)list, (uint64_t)max);
}

void exit(int code) {
    syscall1(SYS_EXIT, (uint64_t)code);
    for (;;) {
    }
}
