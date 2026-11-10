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
#include "arch/x86_64/irq.h"
#include "drivers/pit.h"
#include "drivers/keyboard.h"
#include "drivers/input.h"
#include "tests/ktest.h"

// Stage 4 demo: echo typed characters (keyboard or serial) and show the
// tick counter in the status bar, updated once per second.
__attribute__((noreturn))
static void stage4_echo_loop(void) {
    kprintf("Type something (keyboard or serial):\n");
    uint64_t last_shown = (uint64_t)-1;
    for (;;) {
        uint64_t t = pit_ticks();
        if (t / PIT_HZ != last_shown) {
            last_shown = t / PIT_HZ;
            char status[64];
            ksnprintf(status, sizeof(status), "ticks: %lu  uptime: %lus ", t, last_shown);
            console_set_status(status);
        }
        char c;
        while (input_try_getc(&c)) {
            kprintf("%c", c);
        }
        cpu_hlt();   // sleep until the next IRQ (timer, key, serial)
    }
}

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
    boot_print_memmap();

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

    // Stage 4: hardware interrupts. PIC remap -> IRQ 0-15 = vectors 32-47.
    irq_init();
    pit_init();
    keyboard_init();
    serial_enable_rx_irq();
    cpu_sti();   // from here on, IRQs arrive
    kprintf("PIC remapped, PIT at %d Hz, keyboard + serial input on, interrupts enabled\n", PIT_HZ);

    // Automatic tests (only when booted with "test=...").
    ktest_run();

    stage4_echo_loop();
}
