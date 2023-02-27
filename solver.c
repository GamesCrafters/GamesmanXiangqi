#include "misc.h"
#include "solver.h"
#include "tiersolver.h"
#include "tiertree.h"
#include <stdio.h>

static tier_tree_entry_t *get_tail(TierTreeEntryList *list) {
    if (!list) {
        return NULL;
    }
    while (list->next) {
        list = list->next;
    }
    return list;
}

void solve_local(uint8_t nPiecesMax, uint64_t nthread, uint64_t mem) {
    TierTreeEntryList *solvableTiersHead = tier_tree_init(nPiecesMax, nthread);
    tier_tree_entry_t *solvableTiersTail = get_tail(solvableTiersHead);
    tier_tree_entry_t *tmp;
    tier_solver_stat_t stat;
    TierList *parentTiers, *walker;

    while (solvableTiersHead) {
        stat = solve_tier(solvableTiersHead->tier, nthread, mem);
        if (stat.numLegalPos) {
            /* Solve succeeded. Update tier tree. */
            parentTiers = tier_get_parent_tier_list(solvableTiersHead->tier);
            for (walker = parentTiers; walker; walker = walker->next) {
                tmp = tier_tree_find(walker->tier);
                if (tmp && --tmp->numUnsolvedChildren == 0) {
                    tmp = tier_tree_remove(walker->tier);
                    solvableTiersTail->next = tmp;
                    tmp->next = NULL;
                    solvableTiersTail = tmp;
                }
            }
            tier_list_destroy(parentTiers);
            printf("Tier %s:\n", solvableTiersHead->tier);
            printf("total legal positions: %"PRIu64"\n", stat.numLegalPos);
            printf("number of winning positions: %"PRIu64"\n", stat.numWin);
            printf("number of losing positions: %"PRIu64"\n", stat.numLose);
            printf("number of drawing positions: %"PRIu64"\n", stat.numLegalPos - stat.numWin - stat.numLose);
            printf("longest win for red is %"PRIu64" steps at position %"PRIu64"\n", stat.longestNumStepsToRedWin, stat.longestPosToRedWin);
            printf("longest win for black is %"PRIu64" steps at position %"PRIu64"\n", stat.longestNumStepsToBlackWin, stat.longestPosToBlackWin);
            printf("\n");
            // TODO: process stat
        }
        tmp = solvableTiersHead;
        solvableTiersHead = solvableTiersHead->next;
        free(tmp);
    }
    printf("solve_local: solver done.\n");
}


//printf("total solvable tiers with a maximum of %d pieces: %"PRIu64"\n",
//       nPiecesMax, tierCount96GiB+tierCount384GiB+tierCount1536GiB);
//printf("number of tiers that fit in 96 GiB memory: %"PRIu64"\n", tierCount96GiB);
//printf("number of tiers that fit in 384 GiB memory: %"PRIu64"\n", tierCount384GiB);
//printf("number of tiers that fit in 1536 GiB memory: %"PRIu64"\n", tierCount1536GiB);
//printf("number of tiers ignored: %"PRIu64"\n", tierCountIgnored);
//printf("max solvable tier size: %"PRIu64"\n", maxTierSize);
//printf("total size of all solvable tiers: %"PRIu64"\n", tierSizeTotal);














