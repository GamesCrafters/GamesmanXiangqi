#ifndef TIER_H
#define TIER_H
#include <stdint.h>
#include <stdbool.h>

/* 12 pieces, 2 underscore delimiters, at most 5 pawns on each side, 1 null terminator. */
#define TIER_STR_LENGTH_MAX 25
#define NUM_TIER_SIZE_STEPS 15

typedef struct TierChange {
    int8_t captureIdx;
    int8_t captureRow;
    int8_t pawnIdx;
    int8_t pawnRow;
} tier_change_t;

typedef struct TierListElem {
    struct TierListElem *next;
    struct TierChange change;
    char tier[TIER_STR_LENGTH_MAX];
} TierList;

struct TierArray {
    uint8_t size;
    char **tiers;
    struct TierChange *changes;
};

bool tier_is_legal_tier(const char *tier);

struct TierListElem *tier_get_canonical_tier(const char *tier);
bool tier_is_canonical_tier(const char *tier);

TierList *tier_get_child_tier_list(const char *tier);
TierList *tier_get_parent_tier_list(const char *tier);
bool tier_list_contains(const TierList *list, const char *tier);
void tier_list_destroy(TierList *list);

struct TierArray tier_get_child_tier_array(const char *tier);
void tier_array_destroy(struct TierArray *array);

uint8_t tier_num_child_tiers(const char *tier);
uint8_t tier_num_canonical_child_tiers(const char *tier);
uint64_t *tier_size_steps(const char *tier);
uint64_t tier_size(const char *tier);
uint64_t tier_required_mem(const char *tier);

void tier_get_pawns_per_row(const char *tier, uint8_t *pawnsPerRow);

#endif // TIER_H
