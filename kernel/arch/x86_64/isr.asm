; Interrupt Service Routine stubs: one tiny entry point per vector (0-255).
;
; When an interrupt/exception happens the CPU pushes (on the kernel stack):
;     SS, RSP, RFLAGS, CS, RIP          (always, in 64-bit mode)
;     error code                        (only for some exceptions)
; and jumps to the address in the IDT entry. It does NOT save any other
; register, so we must save all of them before calling C code, and restore
; them in exactly the reverse order before iretq.
;
; To make every frame look the same, stubs for vectors without an error code
; push a fake 0. Then every stub pushes its vector number.
;
; Stack after isr_common pushed everything (low address at the top):
;     r15 r14 r13 r12 r11 r10 r9 r8 rbp rdi rsi rdx rcx rbx rax   <- rsp
;     vector, error_code
;     rip, cs, rflags, rsp, ss                                    (from the CPU)
; This must match struct interrupt_frame in interrupts.h.

bits 64
section .text

extern interrupt_dispatch

; Exception vectors where the CPU pushes an error code.
%macro ISR_ERR 1
isr_%1:
    push qword %1           ; vector number (error code is already on the stack)
    jmp isr_common
%endmacro

%macro ISR_NOERR 1
isr_%1:
    push qword 0            ; fake error code
    push qword %1           ; vector number
    jmp isr_common
%endmacro

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; The CPU aligned RSP to 16 bytes before pushing its 5 values. We added
    ; 2 + 15 = 17 more, so 22 * 8 = 176 bytes in total: still 16-byte aligned,
    ; which the System V ABI requires at a "call".
    mov rdi, rsp            ; arg 1: pointer to struct interrupt_frame
    cld                     ; C code expects the direction flag clear
    call interrupt_dispatch

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16             ; drop vector number and error code
    iretq                   ; pop RIP, CS, RFLAGS, RSP, SS and continue

; Generate the 256 stubs.
%assign i 0
%rep 256
    %if i == 8 || i == 10 || i == 11 || i == 12 || i == 13 || i == 14 || i == 17 || i == 21 || i == 29 || i == 30
        ISR_ERR i
    %else
        ISR_NOERR i
    %endif
    %assign i i + 1
%endrep

; Table of stub addresses, used by idt.c to fill the IDT.
section .rodata
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 256
    dq isr_ %+ i
    %assign i i + 1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
