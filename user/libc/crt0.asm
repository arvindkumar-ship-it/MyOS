; crt0: the first code of every C user program ("C runtime zero").
; The kernel starts us at _start with the stack set up by elf_setup_stack():
;     [rsp] = argc, [rsp + 8] = argv[0], ..., NULL
; We call main(argc, argv) and pass its return value to exit().

bits 64
section .text

extern main
extern exit

global _start
_start:
    xor rbp, rbp            ; end of the frame-pointer chain (for debuggers)
    mov rdi, [rsp]          ; argc
    lea rsi, [rsp + 8]      ; argv
    call main               ; rsp is 16-byte aligned here, as the ABI wants
    mov rdi, rax
    call exit
    ud2                     ; exit does not return

section .note.GNU-stack noalloc noexec nowrite progbits
