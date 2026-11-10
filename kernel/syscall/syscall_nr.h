#pragma once

// System call numbers and error codes, shared by the kernel and user programs.
// (user/include/syscall.inc has the same numbers for assembly programs.)

#define SYS_WRITE   1   // write(fd, buf, len) -> bytes written
#define SYS_EXIT    2   // exit(code) -> does not return
#define SYS_GETPID  3   // getpid() -> pid
#define SYS_SLEEP   4   // sleep(ms) -> 0
#define SYS_READ    5   // read(fd, buf, len) -> bytes read (0 = end of file)
#define SYS_UPTIME  6   // uptime() -> milliseconds since boot
#define SYS_OPEN    7   // open(path, flags) -> fd
#define SYS_CLOSE   8   // close(fd) -> 0
#define SYS_SPAWN   9   // spawn(path, argv) -> pid of a new process running that ELF file
#define SYS_WAIT    10  // wait(pid, &exit_code) -> pid, after that child has exited

// Error codes (returned as negative numbers).
#define ENOENT  2    // no such file or directory
#define E2BIG   7    // argument list too long
#define ENOEXEC 8    // not a valid executable
#define EBADF   9    // bad file descriptor
#define ECHILD  10   // no such child process
#define ENOMEM  12   // out of memory
#define EFAULT  14   // bad pointer
#define ENOTDIR 20   // not a directory
#define EISDIR  21   // is a directory
#define EINVAL  22   // invalid argument
#define EMFILE  24   // too many open files
#define EROFS   30   // read-only file system
#define ENAMETOOLONG 36  // path too long
#define ENOSYS  38   // no such system call
