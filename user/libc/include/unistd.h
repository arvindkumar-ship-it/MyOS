#pragma once

#include <stdint.h>
#include <stddef.h>
#include "syscall_nr.h"

// System call wrappers. Return values < 0 are -errno (see syscall_nr.h).

int64_t write(int fd, const void *buf, size_t len);
int64_t read(int fd, void *buf, size_t len);
int open(const char *path, int flags);
int close(int fd);
int getpid(void);
int sleep_ms(uint64_t ms);
uint64_t uptime_ms(void);
// Start "path" as a new process with arguments argv (NULL-terminated). Returns its pid.
int spawn(const char *path, const char *const *argv);
// Wait for child "pid" to end; *status = its exit code. Returns pid or -ECHILD.
int wait(int pid, int *status);

// Directory entry number "index" of "path": 1 = *entry filled, 0 = no more.
int readdir(const char *path, int index, struct myos_dirent *entry);
int stat(const char *path, struct myos_stat *st);
// Fill up to max entries with the process list; returns how many.
int ps(struct myos_procinfo *list, int max);

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
