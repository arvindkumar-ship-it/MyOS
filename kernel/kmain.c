#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include "drivers/serial.h"
#include "drivers/framebuffer.h"
#include "drivers/console.h"
#include "lib/kprintf.h"
#include "lib/panic.h"
#include "lib/string.h"
#include "arch/x86_64/cpu.h"

// ---------------------------------------------------------------------------
// Limine requests
//
// We do not call Limine. Instead we place "request" structs in a special
// section (.limine_requests). Before jumping to kmain, Limine finds them,
// fills in their .response pointer, and then starts our kernel.
// They are volatile so the compiler does not assume .response is always NULL,
// and "used" so the compiler does not delete them.
// ---------------------------------------------------------------------------

// Which version of the Limine protocol we speak.
__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

// Ask Limine to set up a graphics framebuffer for us.
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

// Start and end markers so Limine knows where the request list begins and ends.
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

// Kernel entry point. Limine jumps here in 64-bit long mode,
// with paging on and the kernel mapped at 0xffffffff80000000.
void kmain(void) {
    // Serial first, so every later step can log what it is doing.
    // (kprintf only prints to serial until the console is ready.)
    // If COM1 is missing, serial_putc just drops characters and we still try the screen.
    serial_init();
    kprintf("MyOS booting...\n");

    // Make sure the bootloader understands our protocol revision.
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        panic("Limine base revision not supported");
    }

    // Make sure we actually got a framebuffer.
    if (framebuffer_request.response == NULL
        || framebuffer_request.response->framebuffer_count < 1) {
        panic("no framebuffer from Limine");
    }

    struct limine_framebuffer *limine_fb = framebuffer_request.response->framebuffers[0];
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

    // Stage 2 checks.
    string_selftest();
    kprintf("string selftest: ok\n");

    // Done test from the guide: must print "deadbeef".
    kprintf("%x\n", 0xDEADBEEF);

    // Every specifier, with the expected output on the next line.
    // volatile: hide the NULL from gcc's compile-time format check; we test it on purpose.
    const char *volatile null_str = NULL;
    kprintf("[%d] [%i] [%u] [%c] [%s] [%s] [%%] [%X]\n",
            -42, 0, 4294967295u, 'A', "str", null_str, 0xBEEFu);
    kprintf("[-42] [0] [4294967295] [A] [str] [(null)] [%%] [BEEF]  <- expected\n");
    kprintf("[%5d] [%05d] [%08x] [%016lx] [%ld] [%p]\n",
            -42, -42, 0x1234u, 0xffffffff80000000ul, (long)INT64_MIN, (void *)kmain);
    kprintf("[  -42] [-0042] [00001234] [ffffffff80000000] [-9223372036854775808] [0x<kmain>]  <- expected\n");

    // Nothing more to do yet.
    halt_forever();
}
