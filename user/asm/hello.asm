; First user program: runs in ring 3 and talks to the kernel only through
; "int 0x80" system calls (rax = number, rdi/rsi/rdx = arguments).

%include "syscall.inc"
bits 64
org USER_BASE

start:
    ; write(1, msg, msg_len)
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rel msg]
    mov rdx, msg_len
    int 0x80

    ; Which ring are we in? The low 2 bits of CS are the current privilege level.
    mov rax, cs
    and rax, 3
    add al, '0'
    mov [rel ring_digit], al
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rel ring_msg]
    mov rdx, ring_msg_len
    int 0x80

    ; getpid(), printed in decimal
    mov rax, SYS_GETPID
    int 0x80
    lea rdi, [rel pid_digits_end]
    call print_number_line

    ; 3 times: sleep(100), write("tick\n")
    mov rbx, 3
.loop:
    mov rax, SYS_SLEEP
    mov rdi, 100
    int 0x80
    mov rax, SYS_WRITE
    mov rdi, 1
    lea rsi, [rel tick]
    mov rdx, tick_len
    int 0x80
    dec rbx
    jnz .loop

    ; exit(0)
    mov rax, SYS_EXIT
    xor rdi, rdi
    int 0x80
    ud2                     ; exit never returns

; Print "pid = <rax>\n". rdi = end of a scratch buffer.
print_number_line:
    mov rcx, 10
    mov byte [rdi - 1], 10  ; '\n'
    lea rsi, [rdi - 1]
.digit:
    xor rdx, rdx
    div rcx                 ; rax = rax / 10, rdx = rax % 10
    add dl, '0'
    dec rsi
    mov [rsi], dl
    test rax, rax
    jnz .digit
    ; prefix "pid = "
    sub rsi, pid_prefix_len
    lea r8, [rel pid_prefix]
    xor rcx, rcx
.copy:
    mov al, [r8 + rcx]
    mov [rsi + rcx], al
    inc rcx
    cmp rcx, pid_prefix_len
    jne .copy
    mov rdx, rdi
    sub rdx, rsi            ; length
    mov rax, SYS_WRITE
    mov rdi, 1
    int 0x80
    ret

msg:        db "Hello from user mode!", 10
msg_len     equ $ - msg
ring_msg:   db "running in ring "
ring_digit: db "?", 10
ring_msg_len equ $ - ring_msg
pid_prefix: db "pid = "
pid_prefix_len equ $ - pid_prefix
tick:       db "tick", 10
tick_len    equ $ - tick
pid_digits: times 32 db 0
pid_digits_end:
