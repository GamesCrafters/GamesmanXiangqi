#include "tier_test.h"
#include "../tiertree.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static bool tier_list_contains(TierList *list, const char *tier) {
    struct TierListElem *walker;
    for (walker = list; walker; walker = walker->next) {
        if (!strcmp(walker->tier, tier)) return true;
    }
    return false;
}

static void test_tier_def(const char *tier) {
    TierList *children = tier_get_child_tier_list(tier);
    struct TierListElem *walker;
    for (walker = children; walker; walker = walker->next) {
        TierList *parents = tier_get_parent_tier_list(walker->tier);
        if (!tier_list_contains(parents, tier)) {
            printf("[%s] is not in its child tier [%s]'s parents list\n", tier, walker->tier);
            printf("parents of [%s]: ", walker->tier);
            for (walker = parents; walker; walker = walker->next) {
                printf("[%s]\n", walker->tier);
            }
            printf("NULL\n");
            exit(1);
        }
        tier_list_destroy(parents);
    }
    tier_list_destroy(children);
}

void tier_test_sanity(void) {
    tier_scan_driver(8, test_tier_def);
    printf("test_tier::tier_test_sanity: passed.\n");
}
