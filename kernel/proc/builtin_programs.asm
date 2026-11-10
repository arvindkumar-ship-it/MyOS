; User programs built into the kernel image (until we have a filesystem).
; Each one is a flat binary from user/asm/, assembled by the Makefile into
; build/user/<name>.bin and copied in here with incbin.
;
; Table (read by proc/programs.c), one entry per program:
;     dq name (C string), start, end

bits 64

%macro PROGRAM 1
    section .rodata
    align 16
    prog_%1_start:
        incbin %strcat("build/user/", %str(%1), ".bin")
    prog_%1_end:
    prog_%1_name:
        db %str(%1), 0
%endmacro

PROGRAM hello
PROGRAM crash_null
PROGRAM crash_priv
PROGRAM crash_kernel
PROGRAM badptr
PROGRAM reader
PROGRAM fastcall
PROGRAM cat_hello

section .rodata
align 8
global builtin_programs
builtin_programs:
%macro ENTRY 1
    dq prog_%1_name, prog_%1_start, prog_%1_end
%endmacro
    ENTRY hello
    ENTRY crash_null
    ENTRY crash_priv
    ENTRY crash_kernel
    ENTRY badptr
    ENTRY reader
    ENTRY fastcall
    ENTRY cat_hello
    dq 0, 0, 0              ; end of table

section .note.GNU-stack noalloc noexec nowrite progbits
