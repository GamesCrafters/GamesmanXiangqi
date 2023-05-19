#include "common.h"
#include "db.h"
#include "frontier.h"
#include "game.h"
#include "misc.h"
#include "tier.h"
#include "tiersolver.h"
#include <malloc.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
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
#define RESERVED_VALUE 0 // Refer to the value table.

static const char *kTier = NULL;       // Tier being solved.
static tier_solver_stat_t stat;        // Tier solver statistics.
static fr_t winFR, loseFR;             // Win and lose frontiers.
static uint64_t **winDivider = NULL;   // Holds the number of positions from each child tier in loseFR (heap).
static uint64_t **loseDivider = NULL;  // Holds the number of positions from each child tier in winFR (heap).
struct TierArray childTiers;           // Array of child tiers (heap).
static uint8_t *nUndChild = NULL;      // Number of undecided child positions array (heap).
static omp_lock_t nUndChildLock;       // Lock for the above array.
static uint16_t *values = NULL;        // Remoteness value array (heap).
static uint64_t tierSize;              // Number of positions in TIER.
static board_t board;                  // Reuse this board for all children/parent generation.

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
    memset(stat, 0, sizeof(*stat));
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
    free(winDivider); winDivider = NULL;
    free(loseDivider); loseDivider = NULL;
}

static bool check_and_load_frontier(uint8_t childIdx, uint64_t hash, uint16_t val) {
    if (!val || val == DRAW_VALUE) return true;
    if (val < DRAW_VALUE) {
        /* LOSE */
        uint16_t rmt = val - 1;
        if (!frontier_add(&loseFR, hash, rmt)) return false;
        #pragma omp atomic
        ++loseDivider[rmt][childIdx];
    } else {
        /* WIN */
        uint16_t rmt = UINT16_MAX - val;
        if (!frontier_add(&winFR, hash, rmt)) return false;
        #pragma omp atomic
        ++winDivider[rmt][childIdx];
    }
    return true;
}

static void accumulate_dividers(uint8_t nChildTiers) {
    #pragma omp parallel for
    for (uint16_t rmt = 0; rmt < FR_SIZE; ++rmt) {
        for (uint8_t childIdx = 1; childIdx < nChildTiers; ++childIdx) {
            winDivider[rmt][childIdx] += winDivider[rmt][childIdx - 1];
            loseDivider[rmt][childIdx] += loseDivider[rmt][childIdx - 1];
        }
    }
}

static bool process_lose_pos(uint16_t childRmt, const char *childPosTier,
                             uint64_t childPosHash,
                             tier_change_t change, board_t *board) {
    uint8_t remChildren;
    pos_array_t parents = game_get_parents(childPosTier, childPosHash, kTier, change, board);
    if (parents.size == ILLEGAL_POSITION_ARRAY_SIZE) { // OOM.
        free(parents.array); parents.array = NULL;
        return false;
    }
    for (uint8_t i = 0; i < parents.size; ++i) {
        omp_set_lock(&nUndChildLock);
        remChildren = nUndChild[parents.array[i]];
        nUndChild[parents.array[i]] = 0;
        omp_unset_lock(&nUndChildLock);
        if (!remChildren) continue;

        /* All parents are win in (childRmt + 1) positions. */
        values[parents.array[i]] = UINT16_MAX - childRmt - 1; // Refer to the value table.
        if (!frontier_add(&winFR, parents.array[i], childRmt + 1)) { // OOM.
            free(parents.array); parents.array = NULL;
            return false;
        }
    }
    free(parents.array); parents.array = NULL;
    return true;
}

static bool process_win_pos(uint16_t childRmt, const char *childPosTier,
                            uint64_t childPosHash,
                            tier_change_t change, board_t *board) {
    uint8_t remChildren;
    pos_array_t parents = game_get_parents(childPosTier, childPosHash, kTier, change, board);
    if (parents.size == ILLEGAL_POSITION_ARRAY_SIZE) { // OOM.
        free(parents.array); parents.array = NULL;
        return false;
    }
    for (uint8_t i = 0; i < parents.size; ++i) {
        omp_set_lock(&nUndChildLock);
        if (!nUndChild[parents.array[i]]) {
            omp_unset_lock(&nUndChildLock);
            continue;
        }
        remChildren = --nUndChild[parents.array[i]];
        omp_unset_lock(&nUndChildLock);

        /* If this child position is the last undecided child of parent position,
           mark parent as lose in (childRmt + 1). */
        if (!remChildren) {
            values[parents.array[i]] = childRmt + 2; // Refer to the value table.
            if (!frontier_add(&loseFR, parents.array[i], childRmt + 1)) { // OOM.
                free(parents.array); parents.array = NULL;
                return false;
            }
        }
    }
    free(parents.array); parents.array = NULL;
    return true;
}

static bool solve_tier_step_0_initialize(const char *tier, uint64_t mem) {    
    uint64_t tierRequiredMem = tier_required_mem(tier);

    /* Zero-initialize solver statistics. */
    init_solver_stat(&stat);
    /* OOM anticipated. */
    if (!tierRequiredMem || tierRequiredMem > mem) {
        printf("tiersolver_solve_tier: early termination due to OOM. Expect to "
               "use %zd bytes of memory, but only %zd bytes are available.\n",
               tierRequiredMem, mem);
        return false;
    }

    init_FR(); // If OOM, there is a bug.
    kTier = tier;
    tierSize = tier_size(tier);
    omp_init_lock(&nUndChildLock);
    game_init_board(&board);
    return true;
}

static bool solve_tier_step_1_0_load_canonical_helper(uint8_t childIdx) {
    bool success = true, loadFRSuccess = true;
    uint64_t childTierSize = tier_size(childTiers.tiers[childIdx]);
    values = db_load_tier(childTiers.tiers[childIdx], childTierSize);
    if (!values) return false; // OOM.

    /* Scan child tier and load winning/losing positions into frontier. */
    #pragma omp parallel for
    for (uint64_t hash = 0; hash < childTierSize; ++hash) {
        loadFRSuccess = check_and_load_frontier(childIdx, hash, values[hash]);
        #pragma omp atomic
        success &= loadFRSuccess;
    }
    free(values); values = NULL;
    return success;
}

static bool solve_tier_step_1_1_load_noncanonical_helper(uint8_t childIdx) {
    bool success = true, loadFRSuccess = true;
    struct TierListElem *canonicalTier = tier_get_canonical_tier(childTiers.tiers[childIdx]);
    if (!canonicalTier) return false; // OOM.
    uint64_t childTierSize = tier_size(canonicalTier->tier);
    values = db_load_tier(canonicalTier->tier, childTierSize);
    if (!values) return false; // OOM.

    /* Scan child tier and load winning/losing positions into frontier. */
    #pragma omp parallel for firstprivate(board)
    for (uint64_t hash = 0; hash < childTierSize; ++hash) {
        uint64_t noncanonicalHash = game_get_noncanonical_hash(
            canonicalTier->tier, hash, childTiers.tiers[childIdx], &board);
        loadFRSuccess = check_and_load_frontier(childIdx, noncanonicalHash, values[hash]);
        #pragma omp atomic
        success &= loadFRSuccess;
    }
    free(canonicalTier); canonicalTier = NULL;
    free(values); values = NULL;
    return success;
}

static bool solve_tier_step_1_load_children(void) {
    /* STEP 1: LOAD ALL WINNING/LOSING POSITIONS FROM
       ALL CHILD TIERS INTO FRONTIER. */
    bool success = true;

    childTiers = tier_get_child_tier_array(kTier); // If OOM, there is a bug.
    init_dividers(childTiers.size); // If OOM, there is a bug.

    /* Child tiers must be processed in series, otherwise the frontier
       dividers wouldn't work. */
    for (uint8_t childIdx = 0; childIdx < childTiers.size; ++childIdx) {
        /* Load child tier from disk */
        bool childIsCanonical = tier_is_canonical_tier(childTiers.tiers[childIdx]);
        if (childIsCanonical) success = solve_tier_step_1_0_load_canonical_helper(childIdx);
        else success = solve_tier_step_1_1_load_noncanonical_helper(childIdx);
        if (!success) return false;
    }
    return true;
}

static bool solve_tier_step_2_setup_solver_arrays(void) {
    /* STEP 2: SET UP SOLVER ARRAYS. */
    values = (uint16_t*)calloc(tierSize, sizeof(uint16_t));
    nUndChild = (uint8_t*)calloc(tierSize, sizeof(uint8_t));
    return values && nUndChild;
}

static bool solve_tier_step_3_scan_tier(void) {
    /* STEP 3: COUNT NUMBER OF CHILDREN OF ALL POSITIONS IN
     * CURRENT TIER AND LOAD PRIMITIVE POSITIONS INTO FRONTIER. */
    bool success = true;

    #pragma omp parallel for firstprivate(board)
    for (uint64_t hash = 0; hash < tierSize; ++hash) {
        nUndChild[hash] = game_num_child_pos(kTier, hash, &board);
        success &= (nUndChild[hash] != ILLEGAL_NUM_CHILD_POS_OOM);
        /* If no children, position is primitive lose. Add it to frontier. */
        if (!nUndChild[hash]) {
            values[hash] = 1;
            success &= frontier_add(&loseFR, hash, 0);
        }
    }
    return success;
}

static uint8_t update_child_idx(uint8_t childIdx, uint64_t **divider, uint16_t rmt, uint64_t i) {
    while (childIdx < childTiers.size) {
        if (i < divider[rmt][childIdx]) break;
        ++childIdx;
    }
    return childIdx;
}

static bool solve_tier_step_4_push_frontier_up(void) {
    /* STEP 4: PUSH FRONTIER UP. */
    const tier_change_t noChange = {INVALID_IDX, -1, INVALID_IDX, -1};
    bool success = true;
    uint8_t childIdx = 0;

    accumulate_dividers(childTiers.size);
    /* Remotenesses must be processed in series. */
    for (uint16_t rmt = 0; rmt < FR_SIZE; ++rmt) {
        /* Process loseFR. */
        childIdx = 0;
        #pragma omp parallel for firstprivate(board, childIdx)
        for (uint64_t i = 0; i < loseFR.sizes[rmt]; ++i) {
            childIdx = update_child_idx(childIdx, loseDivider, rmt, i);
            if (childIdx < childTiers.size) {
                success &= process_lose_pos(rmt, childTiers.tiers[childIdx], loseFR.buckets[rmt][i],
                                            childTiers.changes[childIdx], &board);
            } else {
                success &= process_lose_pos(rmt, kTier, loseFR.buckets[rmt][i], noChange, &board);
            }
        }
        frontier_free(&loseFR, rmt);

        /* Process winFR. */
        childIdx = 0;
        #pragma omp parallel for firstprivate(board, childIdx)
        for (uint64_t i = 0; i < winFR.sizes[rmt]; ++i) {
            childIdx = update_child_idx(childIdx, winDivider, rmt, i);
            if (childIdx < childTiers.size) {
                success &= process_win_pos(rmt, childTiers.tiers[childIdx], winFR.buckets[rmt][i],
                                           childTiers.changes[childIdx], &board);
            } else {
                success &= process_win_pos(rmt, kTier, winFR.buckets[rmt][i], noChange, &board);
                
                /* Update statistics. */
                bool blackTurn = game_is_black_turn(winFR.buckets[rmt][i]);
                if (blackTurn && stat.longestNumStepsToBlackWin < rmt) {
                    stat.longestNumStepsToBlackWin = rmt;
                    stat.longestPosToBlackWin = winFR.buckets[rmt][i];
                } else if (!blackTurn && stat.longestNumStepsToRedWin < rmt) {
                    stat.longestNumStepsToRedWin = rmt;
                    stat.longestPosToRedWin = winFR.buckets[rmt][i];
                }
            }
        }
        frontier_free(&winFR, rmt);
        if (!success) return false;
    }
    destroy_FR();
    destroy_dividers();
    tier_array_destroy(&childTiers);
    return true;
}

static void solve_tier_step_5_mark_draw_positions(void) {
    /* STEP 5: MARK DRAW POSITIONS AND UPDATE STATISTICS. */
    #pragma omp parallel for
    for (uint64_t i = 0; i < tierSize; ++i) {
        if (nUndChild[i] == ILLEGAL_NUM_CHILD_POS) continue;
        if (nUndChild[i]) {
            values[i] = DRAW_VALUE;
            #pragma omp atomic
            ++stat.numLegalPos;
        } else if (values[i] < DRAW_VALUE) {
            #pragma omp atomic
            ++stat.numLose;
            #pragma omp atomic
            ++stat.numLegalPos;
        } else {
            #pragma omp atomic
            ++stat.numWin;
            #pragma omp atomic
            ++stat.numLegalPos;
        }
    }
    free(nUndChild); nUndChild = NULL;
}

static void solve_tier_step_6_save_values(void) {
    /* STEP 6: SAVE SOLVER DATA TO DISK. */
    /* First save the tier file. */
    db_save_tier(kTier, values, tierSize);

    /* Then save the stat file as a success indicator. */
    db_save_stat(kTier, stat);
}

static void solve_tier_step_7_cleanup(void) {
    kTier = NULL;
    destroy_FR();
    destroy_dividers();
    tier_array_destroy(&childTiers);
    free(nUndChild); nUndChild = NULL;
    free(values); values = NULL;
    omp_destroy_lock(&nUndChildLock);
}

/**
 * @brief Solves TIER and returns solver statistics. Assumes all
 * child tiers have been solved and exist in the database.
 * @param tier: tier to be solved.
 * @param mem: amount of available physical memory in Bytes.
 * @return Solver statistics including number of valid positions,
 * number of winning and losing positions, and the longest distance
 * to a red/black win.
 */
tier_solver_stat_t tiersolver_solve_tier(const char *tier, uint64_t mem, bool force) {
    if (force) goto _solve;
    /* If the given TIER is already solved, skip solving and return. */
    int tierStatus = db_check_tier(tier);
    if (tierStatus == DB_TIER_OK) {
        stat = db_load_stat(tier);
        return stat;
    } else if (tierStatus == DB_TIER_STAT_CORRUPTED) {
        // TODO: Fix stat using existing DB file and return stat.
    }

    /* Solver main algorithm. */
_solve:
    if (!solve_tier_step_0_initialize(tier, mem)) goto _bailout;
    if (!solve_tier_step_1_load_children()) goto _bailout;    
    if (!solve_tier_step_2_setup_solver_arrays()) goto _bailout;
    if (!solve_tier_step_3_scan_tier()) goto _bailout;
    if (!solve_tier_step_4_push_frontier_up()) goto _bailout;
    solve_tier_step_5_mark_draw_positions();
    solve_tier_step_6_save_values();

_bailout:
    solve_tier_step_7_cleanup();
    return stat;
}
