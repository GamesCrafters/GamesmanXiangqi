#ifndef MISC_H
#define MISC_H
#include <inttypes.h>
#include <malloc.h>

void *safe_calloc(size_t n, size_t size);
void *safe_malloc(size_t);

#endif // MISC_H
