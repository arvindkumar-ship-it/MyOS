#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "sched/task.h"

// User processes.
//
// User address space layout:
//   0x0000000000400000   program code + data (USER_CODE_BASE)
//   ...
//   0x00007fffffff0000   top of the user stack (grows down, USER_STACK_PAGES)

#define USER_CODE_BASE    0x400000ul
#define USER_STACK_TOP    0x00007ffffff00000ul
#define USER_STACK_PAGES  16   // 64 KiB

// Start a user process from a flat binary: raw machine code assembled with
// "org 0x400000", loaded at USER_CODE_BASE, execution starts at its first byte.
// Returns the new task, or NULL if out of memory.
struct task *process_spawn_flat(const char *name, const void *image, size_t size);

// Start a user process from an ELF file in the VFS (e.g. "/bin/hello") with
// arguments argv[0..argc-1]. "parent": the task that will wait() for it, or
// NULL. A user parent passes on its fds 0-2; otherwise they are /dev/console.
// Returns the task, or NULL with *error set.
struct task *process_spawn_elf(const char *path, int argc, const char *const *argv,
                               struct task *parent, const char **error);

// Copy "len" bytes into another address space at "virt" (pages must be mapped).
// Goes through the HHDM, so it works whatever CR3 currently holds.
void process_copy_to(struct address_space *as, uint64_t virt, const void *src, size_t len);

// Open /dev/console as fd 0, 1 and 2 of a new process.
bool process_open_console(struct task *t);

// Map the user stack in "as". Returns the initial stack pointer, or 0.
uint64_t process_map_stack(struct address_space *as);
