; Crashes with a page fault: writes into the kernel. The page is mapped, but
; without the user bit, so ring 3 may not touch it.
%include "syscall.inc"
bits 64
org USER_BASE
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rel msg]
    mov rdx, msg_len
    int 0x80
    mov rax, 0xffffffff80001000
    mov qword [rax], 0      ; page fault: protection violation, user mode
    ud2
msg: db "crash_kernel: writing to kernel memory...", 10
msg_len equ $ - msg
