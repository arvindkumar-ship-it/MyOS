#pragma once

#include <stddef.h>
#include <stdint.h>

// User programs built into the kernel image (see builtin_programs.asm).

struct builtin_program {
    const char *name;
    const uint8_t *start;
    const uint8_t *end;
};

// Find a built-in program by name. NULL if there is none.
const struct builtin_program *program_find(const char *name);
