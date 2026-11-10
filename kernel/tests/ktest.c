#include "tests/ktest.h"

#include <stddef.h>
#include "tests/tests.h"
#include "lib/cmdline.h"
#include "lib/kprintf.h"
#include "lib/panic.h"
#include "drivers/qemu.h"

struct ktest {
    const char *name;
    void (*fn)(void);
};

// All tests. Each stage adds its own (declared in tests/tests.h).
static const struct ktest tests[] = {
    KTEST_LIST
};

static char selected[32];
static bool active = false;

static bool str_eq(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

void ktest_init(const char *cmdline) {
    active = cmdline_get(cmdline, "test", selected, sizeof(selected));
}

bool ktest_active(void) {
    return active;
}

void ktest_run(void) {
    if (!active) {
        return;
    }
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (str_eq(tests[i].name, selected)) {
            kprintf("TEST START: %s\n", selected);
            tests[i].fn();
            return;
        }
    }
    panic("unknown test '%s'", selected);
}

void ktest_finish(bool passed) {
    kprintf("TEST %s: %s\n", passed ? "PASS" : "FAIL", selected);
    // QEMU exit status will be (value << 1) | 1: 1 -> 3 (pass), 2 -> 5 (fail).
    qemu_exit(passed ? 1 : 2);
}
