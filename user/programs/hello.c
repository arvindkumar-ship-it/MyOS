// Stage 9 Done test: a separately compiled C program, loaded from the initrd
// (/bin/hello) by the kernel's ELF loader.
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    printf("Hello from an ELF program! (pid %d)\n", getpid());
    printf("argc = %d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("argv[%d] = \"%s\"\n", i, argv[i]);
    }
    return 0;
}
