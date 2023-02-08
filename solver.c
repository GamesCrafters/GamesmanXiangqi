#include "misc.h"
#include "solver.h"
#include "tier.h"
#include "tiersolver.h"
#include "tiertree.h"
#include <stdio.h>

static size_t num_tiers(const TierList *list) {
    size_t size = 0;
    while (list) {
        ++size;
        list = list->next;
    }
    return size;
}

static TierTreeEntryList *generate_tier_tree(void) {
    TierTreeEntryList *solvableTiers = NULL;

    return solvableTiers;
}

// TODO
void solve_local(uint64_t nthread, uint64_t mem) {
    TierTreeEntryList *solvableTiers = generate_tier_tree();

    TierTreeEntryList *tmp;
    while (solvableTiers) {
        solver_stat_t stat = solve_tier(solvableTiers->tier, nthread, mem);
        if (stat.numLegalPos) {
            /* Solve succeeded. Update tier tree. */

        }
        tmp = solvableTiers;
        solvableTiers = solvableTiers->next;
        free(tmp);
    }
    printf("solve_local: solver done.");
}
















