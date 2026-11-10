; Uses the "syscall" instruction instead of "int 0x80", checks both give the
; same answer, and times 200000 calls of each.
%include "syscall.inc"
bits 64
org USER_BASE

%define LOOPS 200000

start:
    lea rsi, [rel hello]
    mov rdx, hello_len
    call print

    ; getpid both ways must agree
    mov rax, SYS_GETPID
    syscall
    mov r12, rax
    mov rax, SYS_GETPID
    int 0x80
    cmp rax, r12
    jne mismatch
    lea rsi, [rel same]
    mov rdx, same_len
    call print

    ; time int 0x80
    mov rax, SYS_UPTIME
    syscall
    mov r13, rax
    mov rbx, LOOPS
.slow:
    mov rax, SYS_GETPID
    int 0x80
    dec rbx
    jnz .slow
    mov rax, SYS_UPTIME
    syscall
    sub rax, r13
    mov r14, rax            ; ms for int 0x80

    ; time syscall
    mov rax, SYS_UPTIME
    syscall
    mov r13, rax
    mov rbx, LOOPS
.fast:
    mov rax, SYS_GETPID
    syscall
    dec rbx
    jnz .fast
    mov rax, SYS_UPTIME
    syscall
    sub rax, r13
    mov r15, rax            ; ms for syscall

    lea rsi, [rel t1]
    mov rdx, t1_len
    call print
    mov rax, r14
    call print_u64
    lea rsi, [rel t2]
    mov rdx, t2_len
    call print
    mov rax, r15
    call print_u64
    lea rsi, [rel t3]
    mov rdx, t3_len
    call print

    mov rax, SYS_EXIT
    xor rdi, rdi
    syscall

mismatch:
    lea rsi, [rel bad]
    mov rdx, bad_len
    call print
    mov rax, SYS_EXIT
    mov rdi, 1
    syscall

hello: db "fastcall: hello via the syscall instruction", 10
hello_len equ $ - hello
same:  db "fastcall: getpid via syscall == getpid via int 0x80", 10
same_len equ $ - same
bad:   db "fastcall: getpid results differ!", 10
bad_len equ $ - bad
t1:    db "fastcall: 200000 x getpid: int 0x80 "
t1_len equ $ - t1
t2:    db " ms, syscall "
t2_len equ $ - t2
t3:    db " ms", 10
t3_len equ $ - t3

%include "lib.inc"
