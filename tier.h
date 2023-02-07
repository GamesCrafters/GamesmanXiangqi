#ifndef TIER_H
#define TIER_H
#include <stdint.h>

#define TIER_STR_LENGTH_MAX 25

typedef struct TierListElem {
    struct TierListElem *next;
    char tier[TIER_STR_LENGTH_MAX];
} TierList;

void tier_driver(void);
void tier_driver_multithread(uint64_t nthread);

TierList *child_tiers(const char *tier);
uint64_t tier_size(const char *tier);

TierList *tier_list_insert_head(TierList *list, const char *tier);
void free_tier_list(TierList *list);

#endif // TIER_H
