; Small helper routines for assembly user programs (use "syscall").
; %include this at the END of a program (it contains code and data).

; print: rsi = string, rdx = length
print:
    push rcx
    push r11
    mov rax, SYS_WRITE
    mov rdi, 1
    syscall
    pop r11
    pop rcx
    ret

; print_u64: print rax in decimal (no newline)
print_u64:
    push rbx
    lea rbx, [rel lib_num_end]
    mov rcx, 10
    mov rsi, rbx
.digit:
    xor rdx, rdx
    div rcx
    add dl, '0'
    dec rsi
    mov [rsi], dl
    test rax, rax
    jnz .digit
    mov rdx, rbx
    sub rdx, rsi
    call print
    pop rbx
    ret

lib_num_buf: times 24 db 0
lib_num_end:
