; Passes bad pointers to write(). The kernel must refuse them with -EFAULT
; instead of reading kernel memory (or crashing).
%include "syscall.inc"
bits 64
org USER_BASE
    ; write(1, <kernel address>, 16)
    mov rax, SYS_WRITE
    mov rdi, 1
    mov rsi, 0xffffffff80000000
    mov rdx, 16
    int 0x80
    cmp rax, -EFAULT
    jne fail

    ; write(1, NULL, 5)
    mov rax, SYS_WRITE
    mov rdi, 1
    xor rsi, rsi
    mov rdx, 5
    int 0x80
    cmp rax, -EFAULT
    jne fail

    ; write(1, msg, huge length) - the range wraps around / leaves user space
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rel ok_msg]
    mov rdx, 0xfffffffffffff000
    int 0x80
    cmp rax, -EFAULT
    jne fail

    ; read(0, <read-only?> kernel address, 4) - kernel must not write there
    mov rax, SYS_READ
    xor rdi, rdi
    mov rsi, 0xffffffff80005000
    mov rdx, 4
    int 0x80
    cmp rax, -EFAULT
    jne fail

    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rel ok_msg]
    mov rdx, ok_len
    int 0x80
    mov rax, SYS_EXIT
    xor rdi, rdi
    int 0x80

fail:
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rel fail_msg]
    mov rdx, fail_len
    int 0x80
    mov rax, SYS_EXIT
    mov rdi, 1
    int 0x80

ok_msg:   db "badptr: all bad pointers rejected with EFAULT", 10
ok_len    equ $ - ok_msg
fail_msg: db "badptr: kernel accepted a bad pointer!", 10
fail_len  equ $ - fail_msg
