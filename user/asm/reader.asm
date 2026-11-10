; Reads lines with read(0, ...) and echoes them back with write(1, ...),
; until it reads a line starting with 'q'.
%include "syscall.inc"
bits 64
org USER_BASE
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rel prompt]
    mov rdx, prompt_len
    int 0x80
.again:
    mov rax, SYS_READ
    xor rdi, rdi
    lea rsi, [rel buf]
    mov rdx, 64
    int 0x80                ; rax = bytes read (at least 1)
    mov rdx, rax
    cmp byte [rel buf], 'q'
    je .quit
    mov rax, SYS_WRITE      ; echo what we got
    mov rdi, 1
    lea rsi, [rel buf]
    int 0x80
    jmp .again
.quit:
    mov rax, SYS_EXIT
    mov rdi, 7
    int 0x80

prompt: db "reader: type lines, 'q' quits", 10
prompt_len equ $ - prompt
buf: times 64 db 0
