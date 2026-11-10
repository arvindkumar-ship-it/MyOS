#include "proc/programs.h"

// Table from builtin_programs.asm, ended by an entry with name == NULL.
extern const struct builtin_program builtin_programs[];

static int str_equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

const struct builtin_program *program_find(const char *name) {
    for (const struct builtin_program *p = builtin_programs; p->name != NULL; p++) {
        if (str_equal(p->name, name)) {
            return p;
        }
    }
    return NULL;
}
