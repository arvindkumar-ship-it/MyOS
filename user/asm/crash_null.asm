; Crashes with a page fault: reads address 0 (nothing is mapped there).
%include "syscall.inc"
bits 64
org USER_BASE
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rel msg]
    mov rdx, msg_len
    int 0x80
    mov rax, [abs 0]        ; page fault (abs: the absolute address 0, not rip-relative)
    ud2
msg: db "crash_null: reading address 0...", 10
msg_len equ $ - msg
