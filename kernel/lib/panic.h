#pragma once

// Stop the kernel with a message. Use when something is so wrong that
// continuing could corrupt memory or hide the real bug.
//
//   panic("bad page table at %p", ptr);
//
// The macro adds the source file and line number automatically.
#define panic(...) panic_at(__FILE__, __LINE__, __VA_ARGS__)

// Panic if a condition that must always be true is false.
//   kassert(ptr != NULL);
// #cond turns the condition into a string, so the message shows the exact check.
#define kassert(cond) \
    do { \
        if (!(cond)) { \
            panic("assertion failed: %s", #cond); \
        } \
    } while (0)

__attribute__((noreturn, format(printf, 3, 4)))
void panic_at(const char *file, int line, const char *fmt, ...);
