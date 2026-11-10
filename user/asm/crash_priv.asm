; Crashes with a general protection fault: "cli" is only allowed in ring 0.
%include "syscall.inc"
bits 64
org USER_BASE
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rel msg]
    mov rdx, msg_len
    int 0x80
    cli                     ; #GP in ring 3
    ud2
msg: db "crash_priv: trying cli in ring 3...", 10
msg_len equ $ - msg
