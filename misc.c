#define __STDC_FORMAT_MACROS
#include "misc.h"
#include <stdio.h>
#include <stdlib.h>

void *safe_calloc(size_t n, size_t size) {
    void *ret = calloc(n, size);
    if (!ret) {
        printf("safe_malloc: failed to allocate %"PRIu64
               " elements each of size %"PRIu64".\n", n, size);
        exit(1);
    }
    return ret;
}

void *safe_malloc(size_t n) {
    void *ret = malloc(n);
    if (!ret) {
        printf("safe_malloc: failed to allocate %"PRIu64" bytes.\n", n);
        exit(1);
    }
    return ret;
}
