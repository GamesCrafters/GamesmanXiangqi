#include "misc.h"
#include "tiersolver.h"
#include "tier.h"
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#define FR_SIZE (((UINT16_MAX)-1)>>1)

typedef struct ChildTierPosition {
    uint64_t hash;
    uint16_t remoteness;
} child_tier_pos_t;

static child_tier_pos_t **winFR, **loseFR;
static uint64_t *winFRSize, *loseFRSize;

static void init_FR(void) {
    winFR = (child_tier_pos_t**)safe_calloc(FR_SIZE, sizeof(child_tier_pos_t*));
    loseFR = (child_tier_pos_t**)safe_calloc(FR_SIZE, sizeof(child_tier_pos_t*));
    winFRSize = (uint64_t*)safe_calloc(FR_SIZE, sizeof(uint64_t));
    loseFRSize = (uint64_t*)safe_calloc(FR_SIZE, sizeof(uint64_t));
}

static void destroy_FR(void) {
    for (size_t i = 0; i < FR_SIZE; ++i) {
        if (winFR[i]) free(winFR[i]);
        if (loseFR[i]) free(loseFR[i]);
    }
    free(winFR);
    free(loseFR);
    free(winFRSize);
    free(loseFRSize);
}

static void init_solver_stat(solver_stat_t *stat) {
    stat->numLegalPos = 0;
    stat->numWin = 0;
    stat->numLose = 0;
    stat->longestNumStepsToRedWin = 0;
    stat->longestPosToRedWin = 0;
    stat->longestNumStepsToBlackWin = 0;
    stat->longestPosToBlackWin = 0;
}

static void solver_save_values(const char *tier, uint16_t *values) {
    // TODO
}

static size_t num_tiers(const TierList *list) {
    size_t size = 0;
    while (list) {
        ++size;
        list = list->next;
    }
    return size;
}

// TODO: fill in all psudocode
solver_stat_t solve_tier(const char *tier, uint64_t nthread, uint64_t mem) {
    solver_stat_t stat;
    uint64_t tierSize = tier_size(tier);
    uint64_t childTierSize;
    if (tierSize > mem) {
        /* Not enough memory. */
        stat.numLegalPos = 0;
        return stat;
    }

    /* STEP 0: INITIALIZE STATISTICS FOR CURRENT TIER. */
    init_solver_stat(&stat);

    /* STEP 1: LOAD ALL WIN/LOSE POSITIONS FROM ALL CHILD TIERS INTO FRONTIER. */
    TierList *list = child_tiers(tier);
    size_t numChildTiers = num_tiers(list);
    uint64_t **winDivider = (uint64_t**)safe_malloc(numChildTiers*sizeof(uint64_t*));
    uint64_t **loseDivider = (uint64_t**)safe_malloc(numChildTiers*sizeof(uint64_t*));
    for (size_t i = 0; i < numChildTiers; ++i) {
        winDivider[i] = (uint64_t*)safe_calloc(FR_SIZE, sizeof(uint64_t));
        loseDivider[i] = (uint64_t*)safe_calloc(FR_SIZE, sizeof(uint64_t));
    }

    struct TierListElem *walker = list;
    while (walker) {
        /* Load tier from disk */
        childTierSize = tier_size(walker->tier);
        // TODO

        /* Scan tier and load frontier. */
        for (uint64_t i = 0; i < childTierSize; ++i) {
            // TODO
        }
        walker = walker->next;
    }

    /* STEP 2: SET UP SOLVER ARRAYS. */
    uint16_t *values = (uint16_t*)safe_calloc(tierSize, sizeof(uint16_t));
    uint8_t *numUndecidedChildPos = (uint8_t*)safe_calloc(tierSize, sizeof(uint8_t));

    /* STEP 3: LOAD ALL PRIMITIVE POSITIONS IN CURRENT TIER INTO FRONTIER. */
    for (uint64_t i = 0; i < tierSize; ++i) {
        // TODO: check if position is legal and primitive.
        // If so, add to loseFR. Otherwise, set numUndecidedChildPos.
    }

    /* STEP 4: PUSH FRONTIER UP. */
    for (size_t i = 0; i < FR_SIZE; ++i) {
        /* Process loseFR. */

        /* Process winFR. */
    }
    free_tier_list(list);
    for (size_t i = 0; i < numChildTiers; ++i) {
        free(winDivider[i]);
        free(loseDivider[i]);
    }
    free(winDivider);
    free(loseDivider);

    /* STEP 5: MARK DRAW POSITIONS. */
    for (uint64_t i = 0; i < tierSize; ++i) {
        if (numUndecidedChildPos[i] && values[i] == 0) {
            values[i] = 32768; // value for DRAW
        }
    }
    free(numUndecidedChildPos);

    /* STEP 6: SAVE SOLVER DATA TO DISK. */
    solver_save_values(tier, values);
    free(values);

    return stat;
}
