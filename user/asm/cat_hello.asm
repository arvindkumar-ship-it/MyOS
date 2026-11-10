; Done test of Stage 8: open /hello.txt, read it, print it, close it.
; Also checks the error cases of open/read/close.
%include "syscall.inc"
bits 64
org USER_BASE

start:
    ; fd = open("/hello.txt", O_RDONLY)
    mov rax, SYS_OPEN
    lea rdi, [rel path]
    xor rsi, rsi
    syscall
    test rax, rax
    js fail
    mov r12, rax            ; fd

    lea rsi, [rel opened]
    mov rdx, opened_len
    call print
    mov rax, r12
    call print_u64
    lea rsi, [rel newline]
    mov rdx, 1
    call print

.read_loop:                 ; read 16 bytes at a time until end of file (0)
    mov rax, SYS_READ
    mov rdi, r12
    lea rsi, [rel buf]
    mov rdx, 16
    syscall
    test rax, rax
    js fail
    jz .eof
    mov rdx, rax
    lea rsi, [rel buf]
    call print
    jmp .read_loop
.eof:
    mov rax, SYS_CLOSE
    mov rdi, r12
    syscall
    test rax, rax
    jnz fail

    ; read from the closed fd -> -EBADF
    mov rax, SYS_READ
    mov rdi, r12
    lea rsi, [rel buf]
    mov rdx, 16
    syscall
    cmp rax, -EBADF
    jne fail

    ; open a missing file -> -ENOENT
    mov rax, SYS_OPEN
    lea rdi, [rel missing]
    xor rsi, rsi
    syscall
    cmp rax, -ENOENT
    jne fail

    ; open with a kernel pointer as the path -> -EFAULT
    mov rax, SYS_OPEN
    mov rdi, 0xffffffff80000000
    xor rsi, rsi
    syscall
    cmp rax, -EFAULT
    jne fail

    ; open a file and exit WITHOUT closing it: the kernel must close it
    mov rax, SYS_OPEN
    lea rdi, [rel motd]
    xor rsi, rsi
    syscall

    lea rsi, [rel ok]
    mov rdx, ok_len
    call print
    mov rax, SYS_EXIT
    xor rdi, rdi
    syscall

fail:
    lea rsi, [rel bad]
    mov rdx, bad_len
    call print
    mov rax, SYS_EXIT
    mov rdi, 1
    syscall

path:    db "/hello.txt", 0
missing: db "/no/such/file", 0
motd:    db "/etc/motd", 0
opened:  db "cat_hello: opened /hello.txt as fd "
opened_len equ $ - opened
newline: db 10
ok:      db "cat_hello: error checks ok (EBADF, ENOENT, EFAULT)", 10
ok_len   equ $ - ok
bad:     db "cat_hello: FAILED", 10
bad_len  equ $ - bad
buf:     times 16 db 0

%include "lib.inc"
