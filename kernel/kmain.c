#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

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

// Pack 8-bit red/green/blue into one pixel, using the layout the framebuffer reports.
static uint32_t fb_pixel(struct limine_framebuffer *fb, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << fb->red_mask_shift)
         | ((uint32_t)g << fb->green_mask_shift)
         | ((uint32_t)b << fb->blue_mask_shift);
}

// Draw a blue/green gradient so we can see the kernel really ran.
static void fb_draw_test_pattern(struct limine_framebuffer *fb) {
    // fb->address is a VIRTUAL address (Limine already mapped the framebuffer for us).
    volatile uint32_t *virt_fb = fb->address;
    // pitch = bytes per row. It can be bigger than width * 4, so always use it.
    size_t pixels_per_row = fb->pitch / 4;

    for (size_t y = 0; y < fb->height; y++) {
        for (size_t x = 0; x < fb->width; x++) {
            uint8_t blue  = (uint8_t)(x * 255 / fb->width);
            uint8_t green = (uint8_t)(y * 255 / fb->height);
            virt_fb[y * pixels_per_row + x] = fb_pixel(fb, 0, green, blue);
        }
    }
}

// Kernel entry point. Limine jumps here in 64-bit long mode,
// with paging on and the kernel mapped at 0xffffffff80000000.
void kmain(void) {
    // Make sure the bootloader understands our protocol revision.
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        hcf();
    }

    // Make sure we actually got a framebuffer.
    if (framebuffer_request.response == NULL
        || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];

    // We only handle 32 bits-per-pixel RGB with 8-bit channels.
    if (fb->memory_model != LIMINE_FRAMEBUFFER_RGB || fb->bpp != 32) {
        hcf();
    }

    fb_draw_test_pattern(fb);

    // Nothing more to do yet.
    hcf();
}
