// Tests spawn() and wait() from user space.
#include <stdio.h>
#include <unistd.h>
#include "syscall_nr.h"

static int run(const char *path, const char *const *argv) {
    int pid = spawn(path, argv);
    if (pid < 0) {
        printf("spawntest: spawn(%s) failed: %d\n", path, pid);
        return -1000;
    }
    int status = -1;
    int r = wait(pid, &status);
    printf("spawntest: %s (pid %d) finished, wait() = %d, exit code %d\n", path, pid, r, status);
    return status;
}

int main(void) {
    int ok = 1;

    const char *hello_args[] = { "hello", "one", "two words", "3", NULL };
    ok &= run("/bin/hello", hello_args) == 0;

    const char *exit_args[] = { "exitcode", "42", NULL };
    ok &= run("/bin/exitcode", exit_args) == 42;

    const char *segv_args[] = { "segv", NULL };
    ok &= run("/bin/segv", segv_args) == 128 + 14;   // killed by a page fault (vector 14)

    const char *none[] = { "nope", NULL };
    int r = spawn("/bin/does-not-exist", none);
    printf("spawntest: spawn of a missing file = %d (expected -%d)\n", r, ENOENT);
    ok &= r == -ENOENT;

    const char *notelf[] = { "hello.txt", NULL };
    r = spawn("/hello.txt", notelf);
    printf("spawntest: spawn of a text file = %d (expected -%d)\n", r, ENOEXEC);
    ok &= r == -ENOEXEC;

    r = wait(1, NULL);   // pid 1 is not our child
    printf("spawntest: wait for a non-child = %d (expected -%d)\n", r, ECHILD);
    ok &= r == -ECHILD;

    printf("spawntest: %s\n", ok ? "all ok" : "FAILED");
    return ok ? 0 : 1;
}
