// Exits with the number given as its first argument (for wait() tests).
#include <stdlib.h>

int main(int argc, char **argv) {
    return argc > 1 ? atoi(argv[1]) : 0;
}
