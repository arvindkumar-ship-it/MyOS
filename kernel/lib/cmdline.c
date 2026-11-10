#include "lib/cmdline.h"

bool cmdline_get(const char *cmdline, const char *key, char *buf, size_t buf_size) {
    const char *p = cmdline;
    while (*p) {
        // Skip spaces between words.
        while (*p == ' ') {
            p++;
        }

        // Does this word start with "key="?
        const char *k = key;
        const char *w = p;
        while (*k && *w == *k) {
            k++;
            w++;
        }
        if (*k == '\0' && *w == '=') {
            w++;   // skip '='
            size_t i = 0;
            while (*w && *w != ' ' && i + 1 < buf_size) {
                buf[i++] = *w++;
            }
            buf[i] = '\0';
            return true;
        }

        // Not this word: jump to the next space.
        while (*p && *p != ' ') {
            p++;
        }
    }
    return false;
}
