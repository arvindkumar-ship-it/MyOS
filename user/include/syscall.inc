; System call numbers for assembly user programs.
; Must match kernel/syscall/syscall_nr.h.

%define SYS_WRITE   1
%define SYS_EXIT    2
%define SYS_GETPID  3
%define SYS_SLEEP   4
%define SYS_READ    5
%define SYS_UPTIME  6

%define EFAULT      14

; Every program is a flat binary loaded at this address.
%define USER_BASE   0x400000
