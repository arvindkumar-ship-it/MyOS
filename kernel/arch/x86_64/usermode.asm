; Entering ring 3.
;
; There is no "jump to user mode" instruction. Instead we build the frame an
; interrupt from ring 3 would have pushed, and "return" from that interrupt
; with iretq. iretq pops RIP, CS, RFLAGS, RSP, SS; because the new CS has
; RPL 3, the CPU switches to ring 3 and loads the user stack.

bits 64
section .text

%define USER_DATA_SELECTOR (0x18 | 3)
%define USER_CODE_SELECTOR (0x20 | 3)
%define RFLAGS_IF          0x200
%define RFLAGS_RESERVED    0x2       ; bit 1 is always 1

; void enter_user_mode(uint64_t entry, uint64_t user_rsp)   rdi = entry, rsi = user stack
global enter_user_mode
enter_user_mode:
    mov ax, USER_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax

    push qword USER_DATA_SELECTOR             ; SS
    push rsi                                  ; RSP
    push qword RFLAGS_IF | RFLAGS_RESERVED    ; RFLAGS: interrupts on in user mode
    push qword USER_CODE_SELECTOR             ; CS
    push rdi                                  ; RIP

    ; Do not leak kernel values to the user program: clear every register.
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
