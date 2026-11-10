// Crashes on purpose: writes to address 0.
#include <stdio.h>

int main(void) {
    printf("segv: writing to NULL...\n");
    *(volatile int *)0 = 1;
    printf("segv: not reached\n");
    return 0;
}
