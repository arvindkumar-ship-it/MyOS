// MyOS shell: reads a line, splits it into words, runs the command, repeats.
//
//   builtins:  help, clear, exit [code]
//   others:    /bin/<name> (or a path with '/'), started with spawn() and
//              waited for with wait(), like fork + exec + wait on Unix.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>

#define LINE_MAX 256
#define ARGS_MAX 16

#define CTRL_C 3
#define CTRL_L 12
#define DEL    127

static void clear_screen(void) {
    // '\f' (form feed) clears the MyOS console; the escape codes do the same
    // in a terminal attached to the serial port.
    printf("\f\033[2J\033[H");
}

// Read one line with simple editing. We get every key as it is typed
// (the kernel does not echo), so we echo it ourselves.
// Returns the length, or -1 if the line was cancelled with Ctrl+C.
static int read_line(char *line, int max) {
    int len = 0;
    for (;;) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) {
            continue;
        }
        if (c == '\n' || c == '\r') {
            putchar('\n');
            line[len] = '\0';
            return len;
        }
        if (c == '\b' || c == DEL) {
            if (len > 0) {
                len--;
                printf("\b \b");   // back, overwrite with a space, back again
            }
        } else if (c == CTRL_C) {
            printf("^C\n");
            return -1;
        } else if (c == CTRL_L) {
            clear_screen();
            return -1;
        } else if (c >= ' ' && c <= '~' && len < max - 1) {
            line[len++] = c;
            putchar(c);
        }
    }
}

// Split "line" in place into words. "double quotes" keep spaces inside a word.
static int parse(char *line, char **argv) {
    int argc = 0;
    char *p = line;
    while (argc < ARGS_MAX - 1) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        // Copy the word onto itself without the quote characters.
        char *out = p;
        argv[argc++] = out;
        bool in_quotes = false;
        while (*p != '\0' && (in_quotes || (*p != ' ' && *p != '\t'))) {
            if (*p == '"') {
                in_quotes = !in_quotes;
                p++;
            } else {
                *out++ = *p++;
            }
        }
        if (*p != '\0') {
            p++;   // skip the space after the word
        }
        *out = '\0';
    }
    argv[argc] = NULL;
    return argc;
}

static void help(void) {
    printf("MyOS shell commands:\n");
    printf("  ls [path...]     list a directory (default /)\n");
    printf("  cat file...      print files\n");
    printf("  echo words...    print the words\n");
    printf("  ps               list processes\n");
    printf("  uptime           time since boot\n");
    printf("  clear            clear the screen (also Ctrl+L)\n");
    printf("  help             this list\n");
    printf("  exit [code]      leave the shell\n");
    printf("Other programs in /bin run too, e.g. hello, spawntest.\n");
}

static void run_external(int argc, char **argv) {
    (void)argc;
    char path[LINE_MAX];
    if (strchr(argv[0], '/') != NULL) {
        snprintf(path, sizeof(path), "%s", argv[0]);
    } else {
        snprintf(path, sizeof(path), "/bin/%s", argv[0]);
    }
    int pid = spawn(path, (const char *const *)argv);
    if (pid < 0) {
        if (pid == -ENOENT) {
            printf("sh: %s: command not found\n", argv[0]);
        } else {
            printf("sh: %s: %s\n", argv[0], strerror(-pid));
        }
        return;
    }
    int status = 0;
    wait(pid, &status);
    if (status != 0) {
        printf("[%s exited with code %d]\n", argv[0], status);
    }
}

int main(void) {
    // Message of the day.
    int fd = open("/etc/motd", O_RDONLY);
    if (fd >= 0) {
        char buf[256];
        int64_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            write(STDOUT_FILENO, buf, (size_t)n);
        }
        close(fd);
    }

    char line[LINE_MAX];
    char *argv[ARGS_MAX];
    for (;;) {
        printf("myos> ");
        if (read_line(line, sizeof(line)) < 0) {
            continue;
        }
        int argc = parse(line, argv);
        if (argc == 0) {
            continue;
        }
        if (strcmp(argv[0], "help") == 0) {
            help();
        } else if (strcmp(argv[0], "clear") == 0) {
            clear_screen();
        } else if (strcmp(argv[0], "exit") == 0) {
            return argc > 1 ? atoi(argv[1]) : 0;
        } else {
            run_external(argc, argv);
        }
    }
}
