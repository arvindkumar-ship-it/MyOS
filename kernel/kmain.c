#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "boot.h"
#include "drivers/serial.h"
#include "drivers/framebuffer.h"
#include "drivers/console.h"
#include "lib/kprintf.h"
#include "lib/panic.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/interrupts.h"
#include "tests/ktest.h"

// Kernel entry point. Limine jumps here in 64-bit long mode,
// with paging on and the kernel mapped at 0xffffffff80000000.
void kmain(void) {
    // Serial first, so every later step can log what it is doing.
    // If COM1 is missing, serial_putc just drops characters and we still try the screen.
    serial_init();
    kprintf("MyOS booting...\n");

    // Read and check everything Limine gave us (panics if something is missing).
    boot_init();
    ktest_init(boot_info.cmdline);
    kprintf("cmdline: '%s'\n", boot_info.cmdline);
    kprintf("HHDM offset: %p\n", (void *)boot_info.hhdm_offset);
    kprintf("kernel: phys %p -> virt %p\n",
            (void *)boot_info.kernel_phys_base, (void *)boot_info.kernel_virt_base);

    struct limine_framebuffer *limine_fb = boot_info.framebuffer;
    if (!fb_init(limine_fb)) {
        panic("framebuffer is not 32-bit RGB (bpp=%u)", (unsigned)limine_fb->bpp);
    }
    kprintf("framebuffer: %lux%lu, pitch %lu, virt %p\n",
            limine_fb->width, limine_fb->height, limine_fb->pitch, limine_fb->address);

    if (!console_init()) {
        panic("built-in font is not a valid PSF1 file");
    }
    kprintf("console ready\n");

    kprintf("Hello from MyOS\n");

    // Stage 3: our own GDT + TSS, then the IDT so CPU exceptions are caught.
    gdt_init();
    kprintf("GDT + TSS loaded\n");
    interrupts_init();
    kprintf("IDT loaded (256 vectors)\n");

    // Automatic tests (only when booted with "test=...").
    ktest_run();

    // Nothing more to do yet.
    halt_forever();
}
