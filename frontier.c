#include "frontier.h"
#include "misc.h"
#include <stdio.h>
#include <string.h>

void frontier_init(fr_t *frontier, uint16_t size) {
    frontier->buckets = (uint64_t**)safe_calloc(size, sizeof(uint64_t*));
    frontier->capacities = (uint64_t*)safe_calloc(size, sizeof(uint64_t));
    frontier->sizes = (uint64_t*)safe_calloc(size, sizeof(uint64_t));
    frontier->size = size;
}

void frontier_destroy(fr_t *frontier) {
    if (frontier->buckets) {
        for (uint64_t i = 0; i < frontier->size; ++i) {
            if (frontier->buckets[i]) {
                free(frontier->buckets[i]);
            }
        }
        free(frontier->buckets);
        frontier->buckets = NULL;
    }
    if (frontier->capacities) {
        free(frontier->capacities);
        frontier->capacities = NULL;
    }
    if (frontier->sizes) {
        free(frontier->sizes);
        frontier->sizes = NULL;
    }
}

bool frontier_add(fr_t *frontier, uint64_t hash, uint16_t rmt) {
    if (frontier->sizes[rmt] == frontier->capacities[rmt]) {
        if (frontier->capacities[rmt]) {
            frontier->capacities[rmt] <<= 1;
        } else {
            frontier->capacities[rmt] = 1ULL;
        }
        uint64_t *newBucket = (uint64_t*)malloc(
                    frontier->capacities[rmt] * sizeof(uint64_t)
                    );
        if (!newBucket) return false;
        for (uint64_t i = 0; i < frontier->sizes[rmt]; ++i) {
            newBucket[i] = frontier->buckets[rmt][i];
        }
        free(frontier->buckets[rmt]);
        frontier->buckets[rmt] = newBucket;
    }
    frontier->buckets[rmt][frontier->sizes[rmt]++] = hash;
    return true;
}

void frontier_free(fr_t *frontier, uint16_t rmt) {
    free(frontier->buckets[rmt]);
    frontier->buckets[rmt] = NULL;
}
