// cat: print the contents of files.
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: cat file...\n");
        return 1;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("cat: %s: %s\n", argv[i], strerror(-fd));
            status = 1;
            continue;
        }
        char buf[512];
        int64_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            write(STDOUT_FILENO, buf, (size_t)n);
        }
        if (n < 0) {
            printf("cat: %s: %s\n", argv[i], strerror((int)-n));
            status = 1;
        }
        close(fd);
    }
    return status;
}
