#include "frontier.h"
#include "game.h"
#include "misc.h"
#include "tiersolver.h"
#include "tier.h"
#include <malloc.h>
#include <stdio.h>
#include <string.h>

/**
0: RESERVED – UNREACHEABLE POSITION
1: lose in 0
2: lose in 1
3: lose in 2
…
32767: lose in 32766
32768: draw
32769: win in 32766
32770: win in 32765
32771: win in 32764
…
65535: win in 0
*/

#define FR_SIZE (((UINT16_MAX)-1)>>1)
#define DRAW_VALUE 32768

tier_solver_stat_t stat;               // Tier solver statistics.
static fr_t winFR, loseFR;             // Win and lose frontiers.
static uint64_t **winDivider = NULL;   // Holds the number of positions from each child tier in loseFR (heap).
static uint64_t **loseDivider = NULL;  // Holds the number of positions from each child tier in winFR (heap).
struct TierArray childTiers;           // Array of child tiers (heap).
static uint8_t *nUndChild = NULL;      // Number of undecided child positions array (heap).
static uint16_t *values = NULL;        // Remoteness value array (heap).
static uint64_t *parents = NULL;       // Holds a list of parent positions of a frontier position. (heap)
static uint64_t tierRequiredMem;       // Mem required to solve TIER.
static uint64_t tierSize;              // Number of positions in TIER.

/**
 * @brief Initializes solver frontiers.
 * @note Terminates the program if memory allocation fails.
 */
static void init_FR(void) {
    frontier_init(&winFR, FR_SIZE);
    frontier_init(&loseFR, FR_SIZE);
}

static void destroy_FR(void) {
    frontier_destroy(&winFR);
    frontier_destroy(&loseFR);
}

static void init_solver_stat(tier_solver_stat_t *stat) {
    stat->numLegalPos = 0;
    stat->numWin = 0;
    stat->numLose = 0;
    stat->longestNumStepsToRedWin = 0;
    stat->longestPosToRedWin = 0;
    stat->longestNumStepsToBlackWin = 0;
    stat->longestPosToBlackWin = 0;
}

static void init_dividers(uint8_t nChildTiers) {
    if (!nChildTiers) return;
    winDivider = (uint64_t**)safe_malloc(FR_SIZE*sizeof(uint64_t*));
    loseDivider = (uint64_t**)safe_malloc(FR_SIZE*sizeof(uint64_t*));
    for (uint16_t rmt = 0; rmt < FR_SIZE; ++rmt) {
        winDivider[rmt] = (uint64_t*)safe_calloc(nChildTiers, sizeof(uint64_t));
        loseDivider[rmt] = (uint64_t*)safe_calloc(nChildTiers, sizeof(uint64_t));
    }
}

static void destroy_dividers() {
    /* Both arrays should be freed at the same time. */
    if (!winDivider) return;
    for (uint16_t i = 0; i < FR_SIZE; ++i) {
        free(winDivider[i]);
        free(loseDivider[i]);
    }
    free(winDivider);
    free(loseDivider);
    winDivider = loseDivider = NULL;
}

static void solver_save_values(const char *tier, uint16_t *values, uint64_t tierSize) {
    FILE *savefile = fopen(tier, "wb");
    fwrite(values, sizeof(uint16_t), tierSize, savefile);
    fclose(savefile);
}

static uint16_t *load_values_from_disk(const char *tier, uint64_t size) {
    FILE *loadfile;
    uint16_t *values = (uint16_t*)malloc(size * sizeof(uint16_t));
    if (!values) {
        return NULL;
    }
    loadfile = fopen(tier, "rb");
    fread(values, sizeof(uint16_t), size, loadfile);
    fclose(loadfile);
    return values;
}

static bool check_and_load_frontier(uint8_t childIdx, uint64_t hash, uint16_t val) {
    uint16_t rmt;
    if (!val || val == DRAW_VALUE) return true;
    if (val < DRAW_VALUE) {
        /* LOSE */
        rmt = val - 1;
        if (!frontier_add(&loseFR, hash, rmt)) return false;
        ++loseDivider[childIdx][rmt];
    } else {
        /* WIN */
        rmt = UINT16_MAX - val;
        if (!frontier_add(&winFR, hash, rmt)) return false;
        ++winDivider[childIdx][rmt];
    }
    return true;
}

static void accumulate_dividers(uint8_t nChildTiers) {
    for (uint16_t rmt = 0; rmt < FR_SIZE; ++rmt) {
        for (uint8_t childIdx = 1; childIdx < nChildTiers; ++childIdx) {
            winDivider[rmt][childIdx] += winDivider[rmt][childIdx - 1];
            loseDivider[rmt][childIdx] += loseDivider[rmt][childIdx - 1];
        }
    }
}

static bool process_lose_pos(uint16_t childRmt,
                             const char *childPosTier,
                             uint64_t childPosHash,
                             const char *parentPosTier,
                             board_t *board) {
    parents = game_get_parents(childPosTier, childPosHash, parentPosTier, board);
    if (!parents) return false;
    for (uint8_t i = 0; parents[i] != UINT64_MAX; ++i) {
        /* All parents are win in (childRmt + 1) positions. */
        values[parents[i]] = UINT16_MAX - childRmt - 1; // Refer to the value table.
        nUndChild[parents[i]] = 0;
        frontier_add(&winFR, parents[i], childRmt + 1);
    }
    free(parents);
    return true;
}

static bool process_win_pos(uint16_t childRmt,
                            const char *childPosTier,
                            uint64_t childPosHash,
                            const char *parentPosTier,
                            board_t *board) {
    parents = game_get_parents(childPosTier, childPosHash, parentPosTier, board);
    if (!parents) return false;
    for (uint8_t i = 0; parents[i] != UINT64_MAX; ++i) {
        /* If this child position is the last undecided child of parent position,
           mark parent as lose in (childRmt + 1). */
        if (--nUndChild[parents[i]] == 0) {
            values[parents[i]] = childRmt + 2; // Refer to the value table.
            frontier_add(&loseFR, parents[i], childRmt + 1);
        }
    }
    free(parents);
    return true;
}

/**
 * @brief Solves TIER and returns solver statistics.
 * @param tier: tier to be solved.
 * @param nthread: number of physical threads to use.
 * @param mem: amount of available physical memory in Bytes.
 * @return Solver statistics including number of valid positions,
 * number of winning and losing positions, and the longest distance
 * to a red/black win.
 * @todo Implement solver statistics, parallelize.
 */
tier_solver_stat_t solve_tier(const char *tier, uint64_t nthread, uint64_t mem) {
    (void)nthread; // TODO: parallelize.
    bool success;               // Success flag.
    uint8_t childIdx;           // Child tier index iterator.
    uint16_t rmt;               // Frontier remoteness iterator.
    uint64_t childTierSize;     // Holds the size of a child tier.
    uint64_t i, hash;           // Iterators.
    board_t board;              // Reuse this board for all children/parent generation.

    /* STEP 0: INITIALIZE. */
    tierRequiredMem = tier_required_mem(tier);
    tierSize = tier_size(tier);

    init_solver_stat(&stat); // Initialize to zero.
    if (!tierRequiredMem || tierRequiredMem > mem) {
        /* Not enough memory. */
        return stat;
    }
    init_FR(); // If OOM, there is a bug.

    /* STEP 1: LOAD ALL WIN/LOSE POSITIONS FROM ALL CHILD TIERS INTO FRONTIER. */
    childTiers = tier_get_child_tier_array(tier); // If OOM, there is a bug.
    init_dividers(childTiers.size); // If OOM, there is a bug.
    for (i = 0; i < childTiers.size; ++i) {
        /* Load tier from disk */
        childTierSize = tier_size(childTiers.tiers[i]);
        values = load_values_from_disk(childTiers.tiers[i], childTierSize);
        if (!values) goto bail_out;

        /* Scan tier and load frontier. */
        for (hash = 0; hash < childTierSize; ++hash) {
            if (!check_and_load_frontier(i, hash, values[hash])) {
                goto bail_out;
            }
        }
        free(values);
        values = NULL;
    }

    /* STEP 2: SET UP SOLVER ARRAYS. */
    values = (uint16_t*)calloc(tierSize, sizeof(uint16_t));
    nUndChild = (uint8_t*)calloc(tierSize, sizeof(uint8_t));
    if (!values || !nUndChild) goto bail_out;

    /* STEP 3: COUNT NUMBER OF CHILDREN OF ALL POSITIONS IN
     * CURRENT TIER AND LOAD PRIMITIVE POSITIONS INTO FRONTIER. */
    memset(board.layout, '-', 90);
    for (hash = 0; hash < tierSize; ++hash) {
        nUndChild[hash] = game_num_child_pos(tier, hash, &board);
        if (!nUndChild[hash]) {
            frontier_add(&loseFR, hash, 0);
        }
    }

    /* STEP 4: PUSH FRONTIER UP. */
    accumulate_dividers(childTiers.size);
    for (rmt = 0; rmt < FR_SIZE; ++rmt) {
        /* Process loseFR. */
        i = 0;
        /* Process losing positions loaded from child tiers. */
        for (childIdx = 0; childIdx < childTiers.size; ++childIdx) {
            success = process_lose_pos(rmt, childTiers.tiers[childIdx], loseFR.buckets[rmt][i], tier, &board);
            if (!success) goto bail_out;
        }
        /* Process losing positions in current tier. */
        success = process_lose_pos(rmt, tier, loseFR.buckets[rmt][i], NULL, &board);
        if (!success) goto bail_out;

        /* Process winFR. */
        i = 0;
        /* Process winning positions loaded from child tiers. */
        for (childIdx = 0; childIdx < childTiers.size; ++childIdx) {
            for (; i < winDivider[rmt][childIdx]; ++i) {
                success = process_win_pos(rmt, childTiers.tiers[childIdx], winFR.buckets[rmt][i], tier, &board);
                if (!success) goto bail_out;
            }
        }
        /* Process winning positions in current tier. */
        success = process_win_pos(rmt, tier, winFR.buckets[rmt][i], NULL, &board);
        if (!success) goto bail_out;
    }
    destroy_FR();
    destroy_dividers();
    tier_array_destroy(&childTiers);

    /* STEP 5: MARK DRAW POSITIONS. */
    for (i = 0; i < tierSize; ++i) {
        if (nUndChild[i] && values[i] == 0) {
            values[i] = DRAW_VALUE;
        }
    }
    free(nUndChild);
    nUndChild = NULL;

    /* STEP 6: SAVE SOLVER DATA TO DISK. */
    solver_save_values(tier, values, tierSize);

bail_out:
    destroy_FR();
    destroy_dividers();
    tier_array_destroy(&childTiers);
    free(nUndChild);
    free(parents);
    free(values);
    return stat;
}
