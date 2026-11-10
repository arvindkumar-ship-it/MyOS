; void gdt_load(const struct gdt_pointer *gdtr, uint16_t code_sel, uint16_t data_sel)
; System V ABI: rdi = gdtr, rsi = code selector, rdx = data selector.
;
; lgdt only tells the CPU where the new table is. The segment registers still
; hold the OLD descriptors (cached), so we must reload every one of them.

bits 64
section .text

global gdt_load
gdt_load:
    lgdt [rdi]

    ; Data segment registers can be loaded with a plain mov.
    mov ax, dx
    mov ds, ax
    mov es, ax
    mov ss, ax
    ; fs and gs are not used by the kernel; load the null selector.
    xor ax, ax
    mov fs, ax
    mov gs, ax

    ; CS cannot be loaded with mov. A "far return" pops RIP and then CS,
    ; so we push the new CS and the address to continue at, then retfq.
    push rsi                ; new CS
    lea rax, [rel .reload_cs]
    push rax                ; where to continue
    retfq
.reload_cs:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
