; Context switch between two kernel stacks.
;
; void switch_context(uint64_t *old_rsp_save, uint64_t new_rsp)
;   rdi = where to store the current task's stack pointer
;   rsi = stack pointer of the task to run next
;
; Only the callee-saved registers (rbx, rbp, r12-r15) need saving here:
; switch_context is called like a normal C function, so the caller already
; expects rax, rcx, rdx, rsi, rdi, r8-r11 to be destroyed. rip is saved by
; the "call" that got us here (it is on the stack), and restored by "ret".
;
; Stack of a task that is not running (rsp points at r15):
;     r15 r14 r13 r12 rbp rbx  <return address>

bits 64
section .text

global switch_context
switch_context:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp          ; save old task's stack pointer
    mov rsp, rsi            ; ...and from here on we are on the new task's stack

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret                     ; continue wherever the new task called switch_context

; First code a new task runs. task_create() builds a fake switch_context frame
; whose return address is task_trampoline, with r12 = function and r13 = argument.
extern task_entry
global task_trampoline
task_trampoline:
    mov rdi, r12            ; arg 1: function
    mov rsi, r13            ; arg 2: argument
    call task_entry         ; never returns
    ud2

section .note.GNU-stack noalloc noexec nowrite progbits
