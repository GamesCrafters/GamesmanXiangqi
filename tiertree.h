#ifndef TIERTREE_H
#define TIERTREE_H
#include <stdint.h>
#include "tier.h"

typedef struct TierTreeEntry {
    struct TierTreeEntry *next;
    char tier[TIER_STR_LENGTH_MAX];
    uint8_t numUnsolvedChildren;
} tier_tree_entry_t;

typedef tier_tree_entry_t TierTreeEntryList;

TierTreeEntryList *tier_tree_init(uint8_t nPiecesMax, uint64_t nthread);
void tier_tree_destroy(void);
tier_tree_entry_t *tier_tree_find(const char *tier);
tier_tree_entry_t *tier_tree_remove(const char *tier);

void tier_scan_driver(int nPiecesMax, void (*func)(const char*));
#endif // TIERTREE_H
