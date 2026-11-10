/* Linker script for the MyOS kernel (based on limine-c-template). */
/* It decides at which VIRTUAL address each part of the kernel lives. */

OUTPUT_FORMAT(elf64-x86-64)

/* Limine jumps to this symbol after loading the kernel. */
ENTRY(kmain)

/* Program headers = segments that Limine maps into memory. */
/* FLAGS: 4 = read, 2 = write, 1 = execute. */
PHDRS
{
    limine_requests PT_LOAD FLAGS(0x06); /* read + write */
    text            PT_LOAD FLAGS(0x05); /* read + execute */
    rodata          PT_LOAD FLAGS(0x04); /* read only */
    data            PT_LOAD FLAGS(0x06); /* read + write */
}

SECTIONS
{
    /* Kernel starts at this virtual address: the top 2 GiB of the 64-bit */
    /* address space ("higher half"). The Limine protocol requires the kernel */
    /* to be here, and -mcmodel=kernel expects it too. */
    /* Limine picks the physical address itself and maps it here. */
    . = 0xffffffff80000000;

    /* Limine scans this section to find our requests (framebuffer, memory map, ...). */
    .limine_requests : {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :limine_requests

    /* Each segment starts on a new 4 KiB page, so it can get its own permissions. */
    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    .note.gnu.build-id : {
        *(.note.gnu.build-id)
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .data : {
        *(.data .data.*)
    } :data

    /* .bss (zeroed globals) must be last in :data, so its zeros are not */
    /* stored in the file. Limine zero-fills it at load time. */
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /* Throw away sections the kernel does not need. */
    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
        *(.got .got.*)
    }
}
