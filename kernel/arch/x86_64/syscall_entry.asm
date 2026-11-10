; Fast system calls: the "syscall" / "sysret" instructions.
;
; "int 0x80" goes through the IDT: the CPU checks the gate, loads the kernel
; stack from TSS.rsp0 and pushes a full frame. "syscall" does much less:
;   rcx  <- user RIP (where to return)
;   r11  <- user RFLAGS
;   CS/SS <- kernel selectors from the STAR MSR
;   RIP  <- LSTAR MSR (this code)
;   RFLAGS &= ~FMASK (we clear IF, so no interrupt can arrive here)
; It does NOT switch stacks: RSP is still the USER stack. So the first thing
; we do is switch to the task's kernel stack ourselves.
;
; Then we build the same struct interrupt_frame that isr.asm builds, so the
; C side (and anything that looks at the frame) works the same for both paths.

bits 64
section .text

%define USER_DATA_SELECTOR (0x18 | 3)
%define USER_CODE_SELECTOR (0x20 | 3)

extern sched_kernel_rsp          ; top of the running task's kernel stack (sched.c)
extern syscall_from_frame        ; C handler (syscall.c)

global syscall_entry
syscall_entry:
    ; Interrupts are off (FMASK) and there is one CPU, so a single global is
    ; a safe place to park the user RSP for the next two instructions.
    mov [rel user_rsp_scratch], rsp
    mov rsp, [rel sched_kernel_rsp]

    ; Fake the part of the frame the CPU pushes for an interrupt.
    push qword USER_DATA_SELECTOR          ; ss
    push qword [rel user_rsp_scratch]      ; rsp
    push r11                               ; rflags
    push qword USER_CODE_SELECTOR          ; cs
    push rcx                               ; rip
    push qword 0                           ; error code
    push qword 0x80                        ; "vector": a syscall

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

    mov rdi, rsp            ; struct interrupt_frame *
    cld
    call syscall_from_frame ; sets frame->rax = result

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
    add rsp, 16             ; vector + error code

    ; Now rsp -> rip, cs, rflags, rsp, ss.
    ; sysret jumps to rcx with RFLAGS = r11. If rcx were not a canonical
    ; address, sysret would fault IN RING 0 with the user's stack (a known
    ; x86 security problem), so in that case we leave with iretq instead,
    ; which faults in ring 3 like any bad user jump.
    mov rcx, [rsp]          ; user rip
    mov r11, rcx
    shr r11, 47
    jnz .use_iretq          ; not a lower-half (user) address
    mov r11, [rsp + 16]     ; user rflags
    mov rsp, [rsp + 24]     ; user rsp (interrupts stay off until sysret)
    o64 sysret              ; back to ring 3: RIP = rcx, RFLAGS = r11
.use_iretq:
    iretq

section .bss
user_rsp_scratch: resq 1

section .note.GNU-stack noalloc noexec nowrite progbits
