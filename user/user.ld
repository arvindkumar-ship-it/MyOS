/* Linker script for MyOS user programs (ELF64, loaded by kernel/proc/elf.c). */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

/* One segment per permission. Each starts on a new page, so the kernel can */
/* map code read+execute, rodata read-only and data read+write (+NX). */
PHDRS
{
    text   PT_LOAD FLAGS(5);   /* R X */
    rodata PT_LOAD FLAGS(4);   /* R   */
    data   PT_LOAD FLAGS(6);   /* R W */
}

SECTIONS
{
    . = 0x400000;

    .text : { *(.text .text.*) } :text

    . = ALIGN(4096);
    .rodata : { *(.rodata .rodata.*) } :rodata

    . = ALIGN(4096);
    .data : { *(.data .data.*) } :data
    .bss  : { *(.bss .bss.*) *(COMMON) } :data

    /DISCARD/ : { *(.eh_frame*) *(.note .note.*) *(.comment) }
}
