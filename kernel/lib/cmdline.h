#pragma once

#include <stdbool.h>
#include <stddef.h>

// Kernel command line: space separated "key=value" or "flag" words,
// e.g. "test=div0 quiet". Comes from the "cmdline:" line in limine.conf.

// Copy the value of "key=..." into buf (NUL terminated). Returns false if key is not there.
bool cmdline_get(const char *cmdline, const char *key, char *buf, size_t buf_size);
