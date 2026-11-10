#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include "drivers/serial.h"
#include "drivers/framebuffer.h"
#include "drivers/console.h"

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

// Halt and catch fire: stop doing work forever.
// cli disables interrupts, hlt sleeps the CPU until the next interrupt.
static void hcf(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}

// Kernel entry point. Limine jumps here in 64-bit long mode,
// with paging on and the kernel mapped at 0xffffffff80000000.
void kmain(void) {
    // Serial first, so every later step can log what it is doing.
    serial_init();
    serial_write("Hello from MyOS\n");

    // Make sure the bootloader understands our protocol revision.
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        serial_write("error: Limine base revision not supported\n");
        hcf();
    }

    // Make sure we actually got a framebuffer.
    if (framebuffer_request.response == NULL
        || framebuffer_request.response->framebuffer_count < 1) {
        serial_write("error: no framebuffer from Limine\n");
        hcf();
    }

    if (!fb_init(framebuffer_request.response->framebuffers[0])) {
        serial_write("error: framebuffer is not 32-bit RGB (8 bits per channel)\n");
        hcf();
    }
    serial_write("framebuffer ready\n");

    if (!console_init()) {
        serial_write("error: built-in font is not a valid PSF1 file\n");
        hcf();
    }
    serial_write("console ready\n");

    console_write("Hello from MyOS\n");

    // Nothing more to do yet.
    hcf();
}
