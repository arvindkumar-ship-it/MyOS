; Put the PSF font file inside the kernel image.
; We have no filesystem yet, so the font cannot be loaded from disk.
; "incbin" copies the file's raw bytes here at build time.

section .rodata

global font_psf_start
global font_psf_end

font_psf_start:
    incbin "assets/font.psf"
font_psf_end:

; Tell the linker this file does not need an executable stack.
section .note.GNU-stack noalloc noexec nowrite progbits
