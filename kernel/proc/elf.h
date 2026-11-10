#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "mm/vmm.h"

// ELF64 executables (what gcc + ld produce): a header, a table of "program
// headers" that say which parts of the file go where in memory, and the code
// and data themselves.

#define ELF_MAGIC "\x7f" "ELF"

struct elf64_header {
    uint8_t  ident[16];      // magic, class (2 = 64-bit), data (1 = little endian), version
    uint16_t type;           // 2 = executable
    uint16_t machine;        // 0x3E = x86_64
    uint32_t version;
    uint64_t entry;          // VIRTUAL address of the first instruction
    uint64_t phoff;          // file offset of the program header table
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;      // size of one program header
    uint16_t phnum;          // number of program headers
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    uint32_t type;           // 1 = PT_LOAD: load this into memory
    uint32_t flags;          // PF_X 1, PF_W 2, PF_R 4
    uint64_t offset;         // where the bytes are in the file
    uint64_t vaddr;          // where they go in memory
    uint64_t paddr;
    uint64_t filesz;         // bytes to copy from the file
    uint64_t memsz;          // bytes in memory (rest is zero: .bss)
    uint64_t align;
} __attribute__((packed));

#define ELF_TYPE_EXEC 2
#define ELF_MACHINE_X86_64 0x3E
#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PF_R 4

#define ELF_MAX_ARGS 32   // most arguments a program can get

// Check the file and map + copy every PT_LOAD segment into "as".
// Returns the entry point, or 0 with *error set to a short reason.
uint64_t elf_load(struct address_space *as, const void *file, size_t size, const char **error);

// Build the initial user stack (System V ABI) at the top of the stack mapped
// in "as": the argument strings, then
//     [rsp] argc, argv[0..argc-1], NULL, envp NULL, auxv AT_NULL (0, 0)
// Returns the stack pointer for _start (16-byte aligned), or 0 if it does not fit.
uint64_t elf_setup_stack(struct address_space *as, uint64_t stack_top,
                         int argc, const char *const *argv);
