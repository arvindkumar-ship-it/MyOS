// ps: list all tasks (kernel threads and user processes).
#include <stdio.h>
#include <unistd.h>

int main(void) {
    static struct myos_procinfo list[64];
    int n = ps(list, 64);
    printf("  PID  PPID  STATE     TYPE    TIME  NAME\n");
    for (int i = 0; i < n; i++) {
        struct myos_procinfo *p = &list[i];
        uint64_t ms = p->ticks * 10;
        printf("%5lu %5lu  %-8s  %-6s %3lu.%02lus  %s\n", p->pid, p->ppid, p->state,
               p->is_user ? "user" : "kernel", ms / 1000, (ms % 1000) / 10, p->name);
    }
    return 0;
}
