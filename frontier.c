#include "frontier.h"
#include "misc.h"
#include <stdio.h>
#include <string.h>

void frontier_init(fr_t *frontier, uint16_t size) {
    frontier->size = size;
    frontier->buckets = (uint64_t**)safe_calloc(size, sizeof(uint64_t*));
    frontier->capacities = (uint64_t*)safe_calloc(size, sizeof(uint64_t));
    frontier->sizes = (uint64_t*)safe_calloc(size, sizeof(uint64_t));
    frontier->locks = (omp_lock_t*)safe_calloc(size, sizeof(omp_lock_t));
    for (uint16_t i = 0; i < frontier->size; ++i) {
        omp_init_lock(&frontier->locks[i]);
    }
}

void frontier_destroy(fr_t *frontier) {
    if (frontier->buckets) {
        for (uint16_t i = 0; i < frontier->size; ++i) {
            if (frontier->buckets[i]) {
                free(frontier->buckets[i]);
            }
        }
        free(frontier->buckets); frontier->buckets = NULL;
    }
    free(frontier->capacities); frontier->capacities = NULL;
    free(frontier->sizes); frontier->sizes = NULL;
    if (frontier->locks) {
        for (uint16_t i = 0; i < frontier->size; ++i) {
            omp_destroy_lock(&frontier->locks[i]);
        }
        free(frontier->locks); frontier->locks = NULL;
    }
}

bool frontier_add(fr_t *frontier, uint64_t hash, uint16_t rmt) {
    omp_set_lock(&frontier->locks[rmt]);
    if (frontier->sizes[rmt] == frontier->capacities[rmt]) {
        if (frontier->capacities[rmt]) {
            frontier->capacities[rmt] <<= 1;
        } else {
            frontier->capacities[rmt] = 1ULL;
        }
        uint64_t *newBucket = (uint64_t*)realloc(frontier->buckets[rmt], frontier->capacities[rmt] * sizeof(uint64_t));
        if (!newBucket) {
            omp_unset_lock(&frontier->locks[rmt]);
            return false;
        }
        frontier->buckets[rmt] = newBucket;
    }
    frontier->buckets[rmt][frontier->sizes[rmt]++] = hash;
    omp_unset_lock(&frontier->locks[rmt]);
    return true;
}

void frontier_free(fr_t *frontier, uint16_t rmt) {
    free(frontier->buckets[rmt]); frontier->buckets[rmt] = NULL;
}
