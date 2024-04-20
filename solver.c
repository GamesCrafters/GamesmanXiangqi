#include "common.h"
#include "misc.h"
#include "solver.h"
#include "tiersolver.h"
#include "tiertree.h"
#include <stdio.h>
#include <string.h>

static tier_solver_stat_t globalStat;
static int solvedTiers = 0;
static int skippedTiers = 0;
static int failedTiers = 0;
static int nSolvableTiers = 0;

static void initialize_solver(void) {
    globalStat = (tier_solver_stat_t){0};
    solvedTiers = skippedTiers = failedTiers = nSolvableTiers = 0;
    make_triangle();
}

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

static void update_tier_tree(const char *solvedTier,
                             tier_tree_entry_t **solvableTiersTail,
                             bool canonical_only) {
    tier_tree_entry_t *tmp;
    TierList *parentTiers = tier_get_parent_tier_list(solvedTier);
    TierList *canonicalParents = NULL;
    for (struct TierListElem *walker = parentTiers; walker; walker = walker->next) {
        struct TierListElem *canonical;
        /* Update canonical parent's number of unsolved children only. */
        if (canonical_only) {
            canonical = tier_get_canonical_tier(walker->tier);
        } else {
            canonical = (struct TierListElem *)malloc(sizeof(struct TierListElem));
            strcpy(canonical->tier, walker->tier);
            canonical->next = NULL;
        }
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

static void print_solver_result(const char *functionName) {
    printf("%s: finished solving all tiers:\n"
           "Number of canonical tiers solved: %d\n"
           "Number of non-canonical tiers skipped: %d\n"
           "Number of tiers failed due to OOM: %d\n"
           "Total tiers scanned: %d\n",
           functionName,
           solvedTiers,
           skippedTiers,
           failedTiers,
           solvedTiers + skippedTiers + failedTiers);
    print_stat(globalStat);
    printf("\n");
}

static void solve_tier_tree(TierTreeEntryList *solvable, uint64_t mem,
                            bool force, const char *functionName) {
    tier_tree_entry_t *solvableTail = get_tail(solvable);
    tier_tree_entry_t *tmp;

    while (solvable) {
        /* Only solve canonical tiers. */
        if (tier_is_canonical_tier(solvable->tier)) {
            tier_solver_stat_t stat = 
                tiersolver_solve_tier(solvable->tier, mem, force);
            if (stat.numLegalPos) {
                /* Solve succeeded. Update tier tree. */
                update_tier_tree(solvable->tier, &solvableTail, true);
                update_global_stat(stat);
                printf("Tier %s:\n", solvable->tier);
                print_stat(stat);
                printf("\n");
                ++solvedTiers;
            } else {
                printf("Failed to solve tier %s: not enough memory\n",
                       solvable->tier);
                ++failedTiers;
            }
        } else ++skippedTiers;
        tmp = solvable;
        solvable = solvable->next;
        free(tmp);
        --nSolvableTiers;
        printf("Solvable tiers count: %d\n", nSolvableTiers);
    }
    print_solver_result(functionName);
}

static void aggregate_analysis(analysis_t *dest, const analysis_t *src) {
    dest->hash_size += src->hash_size;
    dest->win_count += src->win_count;
    dest->lose_count += src->lose_count;
    dest->draw_count += src->draw_count;

    for (int i = 0; i < 512; ++i) {
        dest->win_summary[i] += src->win_summary[i];
        dest->lose_summary[i] += src->lose_summary[i];
    }

    if (dest->largest_win_remoteness < src->largest_win_remoteness) {
        dest->largest_win_remoteness = src->largest_win_remoteness;
        strcpy(dest->largest_win_tier, src->largest_win_tier);
        dest->largest_win_pos = src->largest_win_pos;
    }

    if (dest->largest_lose_remoteness < src->largest_lose_remoteness) {
        dest->largest_lose_remoteness = src->largest_lose_remoteness;
        strcpy(dest->largest_lose_tier, src->largest_lose_tier);
        dest->largest_lose_pos = src->largest_lose_pos;
    }
}

static void print_analysis(analysis_t analysis) {
    printf("hash size: %"PRIu64"\n", analysis.hash_size);
    printf("win count: %"PRIu64"\n", analysis.win_count);
    printf("lose count: %"PRIu64"\n", analysis.lose_count);
    printf("draw count: %"PRIu64"\n", analysis.draw_count);
    printf("rmt\twin\tlose\ttotal\n\n\n");
    for (int i = 0; i < 300; ++i) {
        printf("%d\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\n",
               i, analysis.win_summary[i], analysis.lose_summary[i],
               analysis.win_summary[i] + analysis.lose_summary[i]);
    }
    printf("\n\nlongest win in %d steps from tier [%s] position %"PRIu64"\n", 
           analysis.largest_win_remoteness, analysis.largest_win_tier,
           analysis.largest_win_pos);
    printf("\n\nlongest lose in %d steps from tier [%s] position %"PRIu64"\n", 
           analysis.largest_lose_remoteness, analysis.largest_lose_tier,
           analysis.largest_lose_pos);
}

static void count_tier_tree(TierTreeEntryList *solvable) {
    tier_tree_entry_t *solvableTail = get_tail(solvable);
    tier_tree_entry_t *tmp;
    analysis_t global_analysis;
    memset(&global_analysis, 0, sizeof(global_analysis));

    while (solvable) {
        analysis_t analysis;
        if (tier_is_canonical_tier(solvable->tier)) {
            analysis = tiersolver_count_tier(solvable->tier, true);
        } else {
            struct TierListElem *canonical = tier_get_canonical_tier(solvable->tier);
            analysis = tiersolver_count_tier(canonical->tier, false);
            free(canonical);
        }
        aggregate_analysis(&global_analysis, &analysis);
        update_tier_tree(solvable->tier, &solvableTail, false);
        printf("Tier %s scanned\n", solvable->tier);
        tmp = solvable;
        solvable = solvable->next;
        free(tmp);
    }
    
    print_analysis(global_analysis);
}

void solve_local_remaining_pieces(uint8_t nPiecesMax, uint64_t nthread, uint64_t mem, bool force) {
    initialize_solver();
    solve_tier_tree(tier_tree_init(nPiecesMax, nthread), mem, force, "solve_local_remaining_pieces");
}

void count_local_remaining_pieces(uint8_t nPiecesMax, uint64_t nthread) {
    initialize_solver();
    count_tier_tree(tier_tree_init(nPiecesMax, nthread));
}

bool solve_local_single_tier(const char *tier, uint64_t mem) {
    initialize_solver();
    struct TierListElem *canonical = tier_get_canonical_tier(tier);
    struct TierArray childTiers = {0};
    bool ret = false;

    /* Return if the tier has been solved already. */
    int tierStatus = db_check_tier(canonical->tier);
    if (tierStatus == DB_TIER_OK) {
        ret = true;
        goto _bailout;
    }

    /* Recursively solve all child tiers. */
    childTiers = tier_get_child_tier_array(canonical->tier); // If OOM, there is a bug.
    for (uint8_t i = 0; i < childTiers.size; ++i) {
        ret = solve_local_single_tier(childTiers.tiers[i], mem);
        if (!ret) goto _bailout;
    }
    tier_array_destroy(&childTiers);

    /* Solve the given tier. */
    tier_solver_stat_t stat = tiersolver_solve_tier(canonical->tier, mem, false);
    if (stat.numLegalPos) {
        /* Solve succeeded. */
        printf("New tier %s solved:\n", canonical->tier);
        print_stat(stat);
        printf("\n");
        ret = true;
    } else {
        printf("Failed to solve tier %s: not enough memory\n", canonical->tier);
        ret = false;
    }

_bailout:
    tier_array_destroy(&childTiers);
    free(canonical);
    return ret;
}

void solve_local_from_file(const char *filename, uint64_t mem) {
    initialize_solver();
    solve_tier_tree(tier_tree_init_from_file(filename, mem),
                    mem, false, "solve_local_from_file");
}
