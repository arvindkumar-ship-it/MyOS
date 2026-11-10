#pragma once

// System call numbers and error codes, shared by the kernel and user programs.
// (user/include/syscall.inc has the same numbers for assembly programs.)

#define SYS_WRITE   1   // write(fd, buf, len) -> bytes written
#define SYS_EXIT    2   // exit(code) -> does not return
#define SYS_GETPID  3   // getpid() -> pid
#define SYS_SLEEP   4   // sleep(ms) -> 0
#define SYS_READ    5   // read(fd, buf, len) -> bytes read (waits for at least 1)
#define SYS_UPTIME  6   // uptime() -> milliseconds since boot

// Error codes (returned as negative numbers).
#define EBADF   9    // bad file descriptor
#define EFAULT  14   // bad pointer
#define EINVAL  22   // invalid argument
#define ENOSYS  38   // no such system call
