#include "misc.h"
#include "solver.h"
#include "tiersolver.h"
#include "tiertree.h"
#include <stdio.h>
#include <string.h>

static tier_solver_stat_t globalStat;
static int nSolvableTiers = 0;

static tier_tree_entry_t *get_tail(TierTreeEntryList *list) {
    if (!list) {
        nSolvableTiers = 0;
        return NULL;
    }
    nSolvableTiers = 1;
    while (list->next) {
        list = list->next;
        ++nSolvableTiers;
    }
    return list;
}

static void print_stat(tier_solver_stat_t stat) {
    printf("total legal positions: %"PRIu64"\n", stat.numLegalPos);
    printf("number of winning positions: %"PRIu64"\n", stat.numWin);
    printf("number of losing positions: %"PRIu64"\n", stat.numLose);
    printf("number of drawing positions: %"PRIu64"\n",
        stat.numLegalPos - stat.numWin - stat.numLose);
    printf("longest win for red is %"PRIu64" steps at position %"PRIu64
        "\n", stat.longestNumStepsToRedWin, stat.longestPosToRedWin);
    printf("longest win for black is %"PRIu64" steps at position %"PRIu64
        "\n", stat.longestNumStepsToBlackWin, stat.longestPosToBlackWin);
}

static void update_global_stat(tier_solver_stat_t stat) {
    globalStat.numWin += stat.numWin;
    globalStat.numLose += stat.numLose;
    globalStat.numLegalPos += stat.numLegalPos;
    if (stat.longestNumStepsToRedWin > globalStat.longestNumStepsToRedWin) {
        globalStat.longestNumStepsToRedWin = stat.longestNumStepsToRedWin;
        globalStat.longestPosToRedWin = stat.longestPosToRedWin;
    }
    if (stat.longestNumStepsToBlackWin > globalStat.longestNumStepsToBlackWin) {
        globalStat.longestNumStepsToBlackWin = stat.longestNumStepsToBlackWin;
        globalStat.longestPosToBlackWin = stat.longestPosToBlackWin;
    }
}

static void update_tier_tree(const char *solvedTier, tier_tree_entry_t **solvableTiersTail) {
    tier_tree_entry_t *tmp;
    TierList *parentTiers = tier_get_parent_tier_list(solvedTier);
    TierList *canonicalParents = NULL;
    for (struct TierListElem *walker = parentTiers; walker; walker = walker->next) {
        /* Update canonical parent's number of unsolved children only. */
        struct TierListElem *canonical = tier_get_canonical_tier(walker->tier);
        if (tier_list_contains(canonicalParents, canonical->tier)) {
            /* It is possible that a child has two parents that are symmetrical
               to each other. In this case, we should only decrement the child
               counter once. */
            free(canonical);
            continue;
        }
        canonical->next = canonicalParents;
        canonicalParents = canonical;

        tmp = tier_tree_find(canonical->tier);
        if (tmp && --tmp->numUnsolvedChildren == 0) {
            tmp = tier_tree_remove(canonical->tier);
            (*solvableTiersTail)->next = tmp;
            tmp->next = NULL;
            *solvableTiersTail = tmp;
            ++nSolvableTiers;
        }
    }
    tier_list_destroy(canonicalParents);
    tier_list_destroy(parentTiers);
}

void solve_local(uint8_t nPiecesMax, uint64_t nthread, uint64_t mem, bool force) {
    TierTreeEntryList *solvableTiersHead = tier_tree_init(nPiecesMax, nthread);
    tier_tree_entry_t *solvableTiersTail = get_tail(solvableTiersHead);
    tier_tree_entry_t *tmp;
    int solvedTiers = 0, skippedTiers = 0, failedTiers = 0;

    while (solvableTiersHead) {
        /* Only solve canonical tiers. */
        if (tier_is_canonical_tier(solvableTiersHead->tier)) {
            tier_solver_stat_t stat = solve_tier(solvableTiersHead->tier, mem, force);
            if (stat.numLegalPos) {
                /* Solve succeeded. Update tier tree. */
                update_tier_tree(solvableTiersHead->tier, &solvableTiersTail);
                update_global_stat(stat);
                printf("Tier %s:\n", solvableTiersHead->tier);
                print_stat(stat);
                printf("\n");
                ++solvedTiers;
            } else {
                printf("Failed to solve tier %s: not enough memory\n", solvableTiersHead->tier);
                ++failedTiers;
            }
        } else ++skippedTiers;
        tmp = solvableTiersHead;
        solvableTiersHead = solvableTiersHead->next;
        free(tmp);
        --nSolvableTiers;
        printf("Solvable tiers count: %d\n", nSolvableTiers);
    }
    printf("solve_local: finished solving all tiers with less than or equal to %d pieces:\n"
        "Number of canonical tiers solved: %d\n"
        "Number of non-canonical tiers skipped: %d\n"
        "Number of tiers failed due to OOM: %d\n"
        "Total tiers scanned: %d\n",
        2 + nPiecesMax, solvedTiers, skippedTiers, failedTiers, solvedTiers + skippedTiers + failedTiers);
    print_stat(globalStat);
    printf("\n");
}
