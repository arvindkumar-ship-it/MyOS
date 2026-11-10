// ls: list directories (or show a file's size).
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int list(const char *path) {
    struct myos_stat st;
    int r = stat(path, &st);
    if (r < 0) {
        printf("ls: %s: %s\n", path, strerror(-r));
        return 1;
    }
    if (st.type != MYOS_TYPE_DIR) {
        printf("%-28s %8lu\n", path, st.size);
        return 0;
    }
    struct myos_dirent e;
    for (int i = 0; readdir(path, i, &e) == 1; i++) {
        if (e.type == MYOS_TYPE_DIR) {
            char name[MYOS_NAME_MAX + 1];
            snprintf(name, sizeof(name), "%s/", e.name);
            printf("%-28s    <dir>\n", name);
        } else if (e.type == MYOS_TYPE_DEV) {
            printf("%-28s    <dev>\n", e.name);
        } else {
            printf("%-28s %8lu\n", e.name, e.size);
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return list("/");
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (argc > 2) {
            printf("%s:\n", argv[i]);
        }
        status |= list(argv[i]);
    }
    return status;
}
