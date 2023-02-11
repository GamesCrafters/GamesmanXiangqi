#ifndef FRONTIER_H
#define FRONTIER_H
#include <stdint.h>
#include <stdbool.h>

typedef struct Frontier {
    uint64_t **buckets;
    uint64_t *capacities;
    uint64_t *sizes;
    uint64_t size;
} fr_t;

void frontier_init(fr_t *frontier, uint16_t size);
void frontier_destroy(fr_t *frontier);

bool frontier_add(fr_t *frontier, uint64_t hash, uint16_t rmt);
void frontier_free(fr_t *frontier, uint16_t rmt);
uint64_t frontier_get_size(uint16_t rmt);

#endif // FRONTIER_H
