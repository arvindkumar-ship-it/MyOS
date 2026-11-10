#include <stdlib.h>

int atoi(const char *s) {
    int sign = 1, value = 0;
    while (*s == ' ') {
        s++;
    }
    if (*s == '-') {
        sign = -1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        s++;
    }
    return sign * value;
}
