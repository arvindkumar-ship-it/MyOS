#include "boot.h"

#include <stdbool.h>
#include "lib/panic.h"

// ---------------------------------------------------------------------------
// Limine requests
//
// We do not call Limine. Instead we place "request" structs in a special
// section (.limine_requests). Before jumping to kmain, Limine finds them,
// fills in their .response pointer, and then starts our kernel.
// They are volatile so the compiler does not assume .response is always NULL,
// and "used" so the compiler does not delete them.
// ---------------------------------------------------------------------------

#define LIMINE_REQUEST __attribute__((used, section(".limine_requests"))) static volatile

// Which version of the Limine protocol we speak.
LIMINE_REQUEST uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

LIMINE_REQUEST struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0
};

LIMINE_REQUEST struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0
};

LIMINE_REQUEST struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0
};

LIMINE_REQUEST struct limine_executable_address_request kernel_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID, .revision = 0
};

LIMINE_REQUEST struct limine_executable_cmdline_request cmdline_request = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID, .revision = 0
};

LIMINE_REQUEST struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID, .revision = 0
};

// Ask for a 64 KiB boot stack (kmain runs on it until we have tasks).
LIMINE_REQUEST struct limine_stack_size_request stack_size_request = {
    .id = LIMINE_STACK_SIZE_REQUEST_ID, .revision = 0, .stack_size = 64 * 1024
};

// Start and end markers so Limine knows where the request list begins and ends.
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

struct boot_info boot_info;

void boot_init(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        panic("Limine base revision not supported");
    }

    if (framebuffer_request.response == NULL
        || framebuffer_request.response->framebuffer_count < 1) {
        panic("no framebuffer from Limine");
    }
    boot_info.framebuffer = framebuffer_request.response->framebuffers[0];

    if (hhdm_request.response == NULL) {
        panic("no HHDM response from Limine");
    }
    boot_info.hhdm_offset = hhdm_request.response->offset;

    if (memmap_request.response == NULL || memmap_request.response->entry_count == 0) {
        panic("no memory map from Limine");
    }
    boot_info.memmap = memmap_request.response->entries;
    boot_info.memmap_count = memmap_request.response->entry_count;

    if (kernel_address_request.response == NULL) {
        panic("no kernel address response from Limine");
    }
    boot_info.kernel_phys_base = kernel_address_request.response->physical_base;
    boot_info.kernel_virt_base = kernel_address_request.response->virtual_base;

    boot_info.cmdline = "";
    if (cmdline_request.response != NULL && cmdline_request.response->cmdline != NULL) {
        boot_info.cmdline = cmdline_request.response->cmdline;
    }

    boot_info.module_count = 0;
    if (module_request.response != NULL) {
        for (uint64_t i = 0; i < module_request.response->module_count; i++) {
            if (boot_info.module_count == BOOT_MAX_MODULES) {
                break;
            }
            struct limine_file *f = module_request.response->modules[i];
            struct boot_module *m = &boot_info.modules[boot_info.module_count++];
            m->path = f->path;
            m->virt_addr = f->address;
            m->size = f->size;
        }
    }
}
