// uptime: time since boot.
#include <stdio.h>
#include <unistd.h>

int main(void) {
    uint64_t ms = uptime_ms();
    printf("up %lu.%02lu seconds\n", ms / 1000, (ms % 1000) / 10);
    return 0;
}
