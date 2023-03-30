#ifndef FRONTIER_H
#define FRONTIER_H
#include <stdint.h>
#include <stdbool.h>
#include <omp.h>

typedef struct Frontier {
    uint16_t size;
    uint64_t **buckets;
    uint64_t *capacities;
    uint64_t *sizes;
    omp_lock_t *locks;
} fr_t;

void frontier_init(fr_t *frontier, uint16_t size);
void frontier_destroy(fr_t *frontier);

bool frontier_add(fr_t *frontier, uint64_t hash, uint16_t rmt);
void frontier_free(fr_t *frontier, uint16_t rmt);

#endif // FRONTIER_H
