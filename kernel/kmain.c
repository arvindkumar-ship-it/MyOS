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
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "sched/task.h"
#include "tests/ktest.h"

// ---------------------------------------------------------------------------
// Stage 6 demo threads
// ---------------------------------------------------------------------------

// Done test of Stage 6: "A" and "B" print in turns, each sleeping in between.
static void letter_thread(void *arg) {
    const char *letter = arg;
    for (int i = 1; i <= 5; i++) {
        kprintf("%s%d ", letter, i);
        task_sleep_ms(200);
    }
    kprintf("[%s done] ", letter);
}

// Echo typed characters. input_getc() blocks this task until a key arrives.
static void echo_thread(void *arg) {
    (void)arg;
    for (;;) {
        char c = input_getc();
        kprintf("%c", c);
    }
}

// Count tasks for the status bar.
static void count_task(struct task *t, void *ctx) {
    (void)t;
    (*(int *)ctx)++;
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

    // Stage 5: memory management.
    pmm_init();
    vmm_init();
    heap_init();

    // Stage 6: kmain becomes task 0, the timer starts switching tasks.
    sched_init();
    sched_start_preemption();
    kprintf("scheduler running (round robin, 50 ms time slice)\n");

    // Automatic tests (only when booted with "test=...").
    ktest_run();

    // Normal boot: the Stage 6 demo.
    task_create("A", letter_thread, "A");
    task_create("B", letter_thread, "B");
    task_create("echo", echo_thread, NULL);

    // kmain itself keeps the status bar up to date. It runs on Limine's boot
    // stack, so it must never exit.
    for (;;) {
        int tasks = 0;
        task_for_each(count_task, &tasks);
        char status[80];
        ksnprintf(status, sizeof(status), "tasks: %d  switches: %lu  uptime: %lus ",
                  tasks, sched_switch_count(), pit_uptime_ms() / 1000);
        console_set_status(status);
        task_sleep_ms(1000);
    }
}
