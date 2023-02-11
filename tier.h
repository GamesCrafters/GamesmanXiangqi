#ifndef TIER_H
#define TIER_H
#include <stdint.h>

#define TIER_STR_LENGTH_MAX 25

typedef struct TierListElem {
    struct TierListElem *next;
    char tier[TIER_STR_LENGTH_MAX];
} TierList;

struct TierArray {
    uint8_t size;
    char **tiers;
};

TierList *tier_get_child_tier_list(const char *tier);
struct TierArray tier_get_child_tier_array(const char *tier);
uint8_t tier_num_child_tiers(const char *tier);
TierList *parent_tiers(const char *tier);
uint64_t tier_size(const char *tier);
uint64_t tier_required_mem(const char *tier);

TierList *tier_list_insert_head(TierList *list, const char *tier);
void tier_list_destroy(TierList *list);
void tier_array_destroy(struct TierArray *array);

#endif // TIER_H
