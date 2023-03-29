#include "db.h"
#include "frontier.h"
#include "game.h"
#include "misc.h"
#include "tiersolver.h"
#include "tier.h"
#include "common.h"
#include "md5.h"
#include <malloc.h>
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
#define RESERVED_VALUE 0

static const char *kTier = NULL;       // Tier being solved.
static uint64_t kNthread;              // Number of physical threads.
tier_solver_stat_t stat;               // Tier solver statistics.
static fr_t winFR, loseFR;             // Win and lose frontiers.
static uint64_t **winDivider = NULL;   // Holds the number of positions from each child tier in loseFR (heap).
static uint64_t **loseDivider = NULL;  // Holds the number of positions from each child tier in winFR (heap).
struct TierArray childTiers;           // Array of child tiers (heap).
static uint8_t *nUndChild = NULL;      // Number of undecided child positions array (heap).
static uint16_t *values = NULL;        // Remoteness value array (heap).
static pos_array_t parents;            // Holds a list of parent positions of a frontier position. (heap)
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
    free(winDivider); winDivider = NULL;
    free(loseDivider); loseDivider = NULL;
}

static uint16_t *load_values_from_disk(const char *tier, uint64_t size) {
    FILE *loadfile;
    uint16_t *values = (uint16_t*)malloc(size * sizeof(uint16_t));
    if (!values) return NULL;
    loadfile = db_fopen_tier(tier, "rb");
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
        ++loseDivider[rmt][childIdx];
    } else {
        /* WIN */
        rmt = UINT16_MAX - val;
        if (!frontier_add(&winFR, hash, rmt)) return false;
        ++winDivider[rmt][childIdx];
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

static bool process_lose_pos(uint16_t childRmt, const char *childPosTier,
                             uint64_t childPosHash, const char *parentTier,
                             tier_change_t change, board_t *board) {
    parents = game_get_parents(childPosTier, childPosHash, parentTier, change, board);
    if (parents.size == ILLEGAL_POSITION_ARRAY_SIZE) return false; // OOM.
    for (uint8_t i = 0; i < parents.size; ++i) {
        if (nUndChild[parents.array[i]] == 0) continue;
        /* All parents are win in (childRmt + 1) positions. */
        values[parents.array[i]] = UINT16_MAX - childRmt - 1; // Refer to the value table.
        nUndChild[parents.array[i]] = 0;
        if (!frontier_add(&winFR, parents.array[i], childRmt + 1)) return false; // OOM.
    }
    free(parents.array); parents.array = NULL;
    return true;
}

static bool process_win_pos(uint16_t childRmt, const char *childPosTier,
                            uint64_t childPosHash, const char *parentTier,
                            tier_change_t change, board_t *board) {
    parents = game_get_parents(childPosTier, childPosHash, parentTier, change, board);
    if (parents.size == ILLEGAL_POSITION_ARRAY_SIZE) return false; // OOM.
    for (uint8_t i = 0; i < parents.size; ++i) {
        if (nUndChild[parents.array[i]] == 0) continue;
        /* If this child position is the last undecided child of parent position,
           mark parent as lose in (childRmt + 1). */
        if (--nUndChild[parents.array[i]] == 0) {
            values[parents.array[i]] = childRmt + 2; // Refer to the value table.
            if (!frontier_add(&loseFR, parents.array[i], childRmt + 1)) return false; // OOM.
        }
    }
    free(parents.array); parents.array = NULL;
    return true;
}

static bool solve_tier_step_0_initialize(const char *tier, uint64_t nthread, uint64_t mem) {    
    uint64_t tierRequiredMem = tier_required_mem(tier);

    /* Zero-initialize solver statistics. */
    init_solver_stat(&stat);
    /* OOM anticipated. */
    if (!tierRequiredMem || tierRequiredMem > mem) return false;

    init_FR(); // If OOM, there is a bug.
    kTier = tier;
    kNthread = nthread;
    tierSize = tier_size(tier);
    return true;
}

static bool solve_tier_step_1_load_children(void) {
    /* STEP 1: LOAD ALL WINNING/LOSING POSITIONS FROM
       ALL CHILD TIERS INTO FRONTIER. */
    uint8_t childIdx;
    uint64_t hash, childTierSize;

    childTiers = tier_get_child_tier_array(kTier); // If OOM, there is a bug.
    init_dividers(childTiers.size); // If OOM, there is a bug.

    /* For each child tier... */
    for (childIdx = 0; childIdx < childTiers.size; ++childIdx) {
        /* Load child tier from disk */
        childTierSize = tier_size(childTiers.tiers[childIdx]);
        values = load_values_from_disk(childTiers.tiers[childIdx], childTierSize);
        if (!values) return false;

        /* Scan child tier and load winning/losing positions into frontier. */
        for (hash = 0; hash < childTierSize; ++hash) {
            if (!check_and_load_frontier(childIdx, hash, values[hash])) {
                return false;
            }
        }
        free(values); values = NULL;
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
    uint64_t hash;
    game_init_board(&board);
    for (hash = 0; hash < tierSize; ++hash) {
        nUndChild[hash] = game_num_child_pos(kTier, hash, &board);
        if (nUndChild[hash] == ILLEGAL_NUM_CHILD_POS_OOM) return false;
        if (!nUndChild[hash]) {
            values[hash] = 1;
            if (!frontier_add(&loseFR, hash, 0)) return false;
        }
    }
    return true;
}

static bool solve_tier_step_4_push_frontier_up(void) {
    /* STEP 4: PUSH FRONTIER UP. */
    bool success;
    uint8_t childIdx;
    uint64_t i;
    tier_change_t noChange;
    noChange.captureIdx = noChange.pawnIdx = INVALID_IDX;
    noChange.captureRow = noChange.pawnRow = -1;

    accumulate_dividers(childTiers.size);
    for (uint16_t rmt = 0; rmt < FR_SIZE; ++rmt) {
        /* Process loseFR. */
        /* Process losing positions loaded from child tiers. */
        i = 0;
        for (childIdx = 0; childIdx < childTiers.size; ++childIdx) {
            for (; i < loseDivider[rmt][childIdx]; ++i) {
                success = process_lose_pos(rmt, childTiers.tiers[childIdx], loseFR.buckets[rmt][i],
                                           kTier, childTiers.changes[childIdx], &board);
                if (!success) return false;
            }
        }
        /* Process losing positions in current tier. */
        for (; i < loseFR.sizes[rmt]; ++i) {
            success = process_lose_pos(rmt, kTier, loseFR.buckets[rmt][i], kTier, noChange, &board);
            if (!success) return false;
        }

        /* Process winFR. */
        /* Process winning positions loaded from child tiers. */
        i = 0;
        for (childIdx = 0; childIdx < childTiers.size; ++childIdx) {
            for (; i < winDivider[rmt][childIdx]; ++i) {
                success = process_win_pos(rmt, childTiers.tiers[childIdx], winFR.buckets[rmt][i],
                                          kTier, childTiers.changes[childIdx], &board);
                if (!success) return false;
            }
        }
        /* Process winning positions in current tier. */
        for (; i < winFR.sizes[rmt]; ++i) {
            success = process_win_pos(rmt, kTier, winFR.buckets[rmt][i], kTier, noChange, &board);
            if (!success) return false;
            /* Update statistics. */
            if (game_is_black_turn(winFR.buckets[rmt][i])) {
                stat.longestPosToBlackWin = winFR.buckets[rmt][i];
                stat.longestNumStepsToBlackWin = rmt;
            } else {
                stat.longestPosToRedWin = winFR.buckets[rmt][i];
                stat.longestNumStepsToRedWin = rmt;
            }
        }
    }
    destroy_FR();
    destroy_dividers();
    tier_array_destroy(&childTiers);
    return true;
}

static void solve_tier_step_5_mark_draw_positions(void) {
    /* STEP 5: MARK DRAW POSITIONS AND UPDATE STATISTICS. */
    for (uint64_t i = 0; i < tierSize; ++i) {
        if (nUndChild[i] == ILLEGAL_NUM_CHILD_POS) continue;
        if (nUndChild[i]) {
            values[i] = DRAW_VALUE;
            ++stat.numLegalPos;
        } else if (values[i] < DRAW_VALUE) {
            ++stat.numLose;
            ++stat.numLegalPos;
        } else {
            ++stat.numWin;
            ++stat.numLegalPos;
        }
    }
    free(nUndChild); nUndChild = NULL;
}

static bool solve_tier_step_6_0_check_tier_file(void) {
    int db_tier_status = db_check_tier(kTier);
    if (db_tier_status == DB_TIER_MISSING) return false;

    /* Check if tier file already exists and identical to new data. */
    FILE *fp = db_fopen_tier(kTier, "rb");
    if (!fp) return false;
    MD5_CTX filectx = MDFile(fp);
    fclose(fp);
    MD5_CTX valuectx = MDData(values, tierSize * sizeof(uint16_t));
    if (memcmp(filectx.digest, valuectx.digest, 16)) {
        printf("fatal error: new solver result does not match old database.\n");
        exit(1);
    }
    printf("md5 test passed.\n");
    return true;
}

static void solve_tier_step_6_save_values(void) {
    /* STEP 6: SAVE SOLVER DATA TO DISK. */
    /* First save the tier file. */
    FILE *fp;
    if (!solve_tier_step_6_0_check_tier_file()) {
        fp = db_fopen_tier(kTier, "wb");
        fwrite(values, sizeof(uint16_t), tierSize, fp);
        fclose(fp);
    }

    /* Then save the stat file as a success indicator. */
    fp = db_fopen_stat(kTier, "wb");
    fwrite(&stat, sizeof(stat), 1, fp);
    fclose(fp);
}

static void solve_tier_step_7_cleanup(void) {
    kTier = NULL;
    destroy_FR();
    destroy_dividers();
    tier_array_destroy(&childTiers);
    free(nUndChild); nUndChild = NULL;
    free(parents.array); parents.array = NULL;
    free(values); values = NULL;
}

/**
 * @brief Solves TIER and returns solver statistics.
 * @param tier: tier to be solved.
 * @param nthread: number of physical threads to use.
 * @param mem: amount of available physical memory in Bytes.
 * @return Solver statistics including number of valid positions,
 * number of winning and losing positions, and the longest distance
 * to a red/black win.
 */
tier_solver_stat_t solve_tier(const char *tier, uint64_t nthread, uint64_t mem, bool force) {
    if (force) goto _solve;
    /* If the given TIER is already solved, skip solving and return. */
    int db_tier_status = db_check_tier(tier);
    if (db_tier_status == DB_TIER_OK) {
        stat = db_load_stat(tier);
        return stat;
    } else if (db_tier_status == DB_TIER_STAT_CORRUPTED) {
        // TODO: Fix stat using existing DB file and return stat.
    }

    /* Solver main algorithm. */
_solve:
    if (!solve_tier_step_0_initialize(tier, nthread, mem)) goto _bailout;
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
