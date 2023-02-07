#define __STDC_FORMAT_MACROS
#include "tier.h"
#include "types.h"
#include "misc.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* Settings */
static const int END_GAME_PIECES_MAX = 3;

/**
 * Tier Hash Format:
 *     [REMAINING_PIECES]_[RED_PAWN_ROWS]_[BLACK_PAWN_ROWS]
 *
 * where REMAINING_PIECES is a 12-digit string representing
 * the number of remaining pieces of each type:
 *     +-----------------------------------------------+
 *     | A | a | B | b | P | p | N | n | C | c | R | r |
 *     +-----------------------------------------------+
 *       0   1   2   3   4   5   6   7   8   9  10  11
 * A: advisors, B: bishops, P: pawns, N: knights, C: cannons,
 * R: rooks. Capital letters for red, lower case letters for
 * black.
 *
 * RED_PAWN_ROWS is an empty string if there are no red pawns
 * left on the board as indicated by REMAINING_PIECES, or a
 * non-increasing P-digit string representing the rows that
 * contain a red pawn. Starting from 0, we count the row number
 * from the bottom of black's side. For example, if there
 * are 3 red pawns left on the board (P==3), two of them are on
 * row 4 and one of them is on row 2, then RED_PAWN_ROWS=="422".
 * A pawn can never reach rows 7-9 according to the rules.
 *
 * BLACK_PAWN_ROWS has the exact same formatting as RED_PAWN_ROWS
 * except that we start counting the row number from the bottom
 * row of red's side.
 */

/* There are only 90 slots on a Chinese chess board;
   maximum number of a type of pieces is 5, so we
   only need n choose k<=5 values. */
#define CHOOSE_ROWS 91
#define CHOOSE_COLS 6
static uint64_t choose[CHOOSE_ROWS][CHOOSE_COLS];

/* Max number of remaining pieces of each type. */
static const char *REM_MAX = "222255222222";

/* Statistics */
typedef struct TierStat {
    uint64_t maxTierSize;
    uint64_t tierSizeTotal;
    uint64_t tierCount96GiB;
    uint64_t tierCount384GiB;
    uint64_t tierCount1536GiB;
    uint64_t tierCountIgnored;
} tier_stat_t;

static uint64_t maxTierSize = 0ULL;
static uint64_t tierSizeTotal = 0ULL;
static uint64_t tierCount96GiB = 0ULL;
static uint64_t tierCount384GiB = 0ULL;
static uint64_t tierCount1536GiB = 0ULL;
static uint64_t tierCountIgnored = 0ULL;

static uint64_t safe_add_uint64(uint64_t lhs, uint64_t rhs) {
    if (!lhs || !rhs || lhs > UINT64_MAX - rhs) {
        return 0;
    }
    return lhs + rhs;
}

static uint64_t safe_mult_uint64(uint64_t lhs, uint64_t rhs) {
    if (!lhs || !rhs || lhs > UINT64_MAX / rhs) {
        return 0;
    }
    return lhs * rhs;
}

static void make_triangle(void) {
    int i, j;
    choose[0][0] = 1;
    for (i = 1; i < CHOOSE_ROWS; ++i) {
        choose[i][0] = 1;
        for (j = 1; j <= (i < CHOOSE_COLS-1 ? i : CHOOSE_COLS-1); ++j) {
            choose[i][j] = choose[i - 1][j - 1] + choose[i - 1][j];
        }
    }
}

TierList *tier_list_insert_head(TierList *list, const char *tier) {
    TierList *newHead = (TierList *)safe_malloc(sizeof(struct TierListElem));
    newHead->next = list;
    for (int i = 0; i < TIER_STR_LENGTH_MAX; ++i) {
        newHead->tier[i] = tier[i];
    }
    return newHead;
}

void free_tier_list(TierList *list) {
    struct TierListElem *next;
    while (list) {
        next = list->next;
        free(list);
        list = next;
    }
}

static void str_shift_left(char *str, int idx) {
    while (str[idx]) {
        str[idx] = str[idx + 1];
        ++idx;
    }
}

static void str_insert(char *str, char c, int idx) {
    char tmp;
    while (c) {
        tmp = str[idx];
        str[idx] = c;
        c = tmp;
        ++idx;
    }
    str[idx] = '\0';
}

static TierList *remove_piece_and_insert(TierList *list, char *tier, int idx) {
    int redPawnTotal = tier[RED_P_IDX] - '0';
    int blackPawnTotal = tier[BLACK_P_IDX] - '0';
    char hold;

    --tier[idx];
    /* If we are removing a pawn, we need to update the pawn list accordingly. */
    if (idx == RED_P_IDX) {
        for (int i = 13; i < 13 + redPawnTotal; ++i) {
            while (tier[i] == tier[i + 1]) ++i;
            hold = tier[i];
            str_shift_left(tier, i);
            list = tier_list_insert_head(list, tier);
            str_insert(tier, hold, i);
        }
    } else if (idx == BLACK_P_IDX) {
        for (int i = 14+redPawnTotal; i < 14+redPawnTotal+blackPawnTotal; ++i) {
            while (tier[i] == tier[i + 1]) ++i;
            hold = tier[i];
            str_shift_left(tier, i);
            list = tier_list_insert_head(list, tier);
            str_insert(tier, hold, i);
        }
    } else {
        list = tier_list_insert_head(list, tier);
    }
    ++tier[idx];
    return list;
}

/**
 * @brief Returns a linked list of child tiers of the given TIER.
 * @param tier: generate child tiers of this tier.
 * @return A linked list of child tiers.
 * @todo This function can be further optimized by tightening
 * the restrictions on when a piece can be captured.
 */
TierList *child_tiers(const char *tier) {
    int redPawnTotal = tier[RED_P_IDX] - '0';
    int blackPawnTotal = tier[BLACK_P_IDX] - '0';
    int i;
    TierList *list = NULL;
    char tierCpy[TIER_STR_LENGTH_MAX];
    for (int i = 0; i < TIER_STR_LENGTH_MAX; ++i) {
        tierCpy[i] = tier[i];
    }
    bool redCanCrossRiver = tier[RED_P_IDX] > '0' || tier[RED_N_IDX] > '0' ||
            tier[RED_C_IDX] > '0' || tier[RED_R_IDX] > '0';
    bool blackCanCrossRiver = tier[BLACK_P_IDX] > '0' || tier[BLACK_N_IDX] > '0' ||
            tier[BLACK_C_IDX] > '0' || tier[BLACK_R_IDX] > '0';

    /* 1. Child tiers by capturing. */

    /* Advisors and bishops can only be captured if there
       exists at least one opponent piece that can cross
       the river. */
    if (blackCanCrossRiver && tier[RED_A_IDX] > '0') {
        list = remove_piece_and_insert(list, tierCpy, RED_A_IDX);
    }
    if (blackCanCrossRiver && tier[RED_B_IDX] > '0') {
        list = remove_piece_and_insert(list, tierCpy, RED_B_IDX);
    }
    if (redCanCrossRiver && tier[BLACK_A_IDX] > '0') {
        list = remove_piece_and_insert(list, tierCpy, BLACK_A_IDX);
    }
    if (redCanCrossRiver && tier[BLACK_B_IDX] > '0') {
        list = remove_piece_and_insert(list, tierCpy, BLACK_B_IDX);
    }

    /* All other pieces can be captured by the king. */
    for (i = RED_P_IDX; i <= BLACK_R_IDX; ++i) {
        if (tier[i] > '0') {
            list = remove_piece_and_insert(list, tierCpy, i);
        }
    }

    /* 2. Child tiers by forward pawn moves. */

    /* Check all available forward red pawn moves. Note that the row
       numbers of pawns are sorted in descending order, so we can
       stop the loop once we see a pawn on the 0th row, which is the
       bottom row on the opponent's side. */
    for (i = 13; i < 13 + redPawnTotal && tier[i] > '0'; ++i) {
        /* Skip current pawn if next pawn is also on the same row
           because moving either pawn gives the same tier. No need
           to check if i+1 is out of bounds. */
        while (tier[i] == tier[i+1]) ++i;
        --tierCpy[i];
        list = tier_list_insert_head(list, tierCpy);
        ++tierCpy[i];
    }

    /* Check forward black pawn moves. */
    for (i = 14+redPawnTotal; i < 14+redPawnTotal+blackPawnTotal && tier[i] > '0'; ++i) {
        while (tier[i] == tier[i+1]) ++i;
        --tierCpy[i];
        list = tier_list_insert_head(list, tierCpy);
        ++tierCpy[i];
    }

    return list;
}

/**
 * @brief Returns the number of rearrangements of pieces at
 * tier size calculation step STEP.
 * The calculation of tier size is divided into 20 steps.
 * Step 0: red king and advisors.
 * Step 1: black king and advisors.
 * Step 2: red bishops.
 * Step 3: black bishops.
 * Step 4-13: pawns on each row of the board.
 * Step 14: red knight.
 * Step 15: black knight.
 * Step 16: red cannon.
 * Step 17: black cannon.
 * Step 18: red rook.
 * Step 19: black rook.
 * @param tier: Tier string.
 * @param step: Tier size calculation step number.
 * @return Number of rearrangements of pieces at
 * tier size calculation step STEP.
 */
static uint64_t tier_size_step(const char *tier, int step) {
    int redPawnTotal = tier[RED_P_IDX] - '0';
    int blackPawnTotal = tier[BLACK_P_IDX] - '0';
    int redPawnRow = 0, blackPawnRow = 0;
    int i;
    switch (step) {
    case 0: case 1: // King and advisors.
        switch (tier[RED_A_IDX + step]) {
        case '0':
            /* If there are no advisors, there are 9 slots
               for the king. */
            return 9ULL;
        case '1':
            /* King takes one of the 5 advisor slots: 5*nCr(5-1, 1);
               King is in one of the other 4 slots: 4*nCr(5, 1). */
            return 40ULL;
        case '2':
            /* King takes one of the 5 advisor slots: 5*nCr(5-1, 2);
               King is in one of the other 4 slots: 4*nCr(5, 2). */
            return 70ULL;
        }

    case 2: case 3: // Bishops.
        /* There are 7 possible slots that a bishop can be in. */
        return choose[7][tier[RED_B_IDX + step - 2] - '0'];

        /* Define row number to be 0 thru 9 where 0 is the bottom line of
       black side and 9 is the bottom line of red side. */
    case 4: case 5: case 6: {
        /* Bottom three rows of black's half-board. No black pawns should be found.
           There are nCr(9, red) ways to place red pawns on the specified row. */
        for (i = 13; i < 13 + redPawnTotal; ++i) {
            redPawnRow += (tier[i] - '0' == step - 4);
        }
        return choose[9][redPawnRow];
    }

    case 7: case 8: case 9: case 10: {
        for (i = 13; i < 13 + redPawnTotal; ++i) {
            redPawnRow += (tier[i] - '0' == step - 4);
        }
        for (i = 14 + redPawnTotal; i < 14 + redPawnTotal + blackPawnTotal; ++i) {
            blackPawnRow += (9 - (tier[i] - '0') == step - 4);
        }
        if (step < 9) {
            /* Top two rows of black's half-board. Any black pawn in these two rows
               can only appear in one of the 5 special columns. There are
               nCr(5, black)*nCr(9-black, red) ways to place all pawns on the
               specified row. */
            return choose[5][blackPawnRow] * choose[9 - blackPawnRow][redPawnRow];
        } else {
            /* Top two rows of red's half-board. Similar to the case above.
               nCr(5, red)*nCr(9-red, black). */
            return choose[5][redPawnRow] * choose[9 - redPawnRow][blackPawnRow];
        }
    }

    case 11: case 12: case 13: {
        /* Bottom three rows of red's half-board. No red pawns should be found.
           There are nCr(9, black) ways to place black pawns on the specified row. */
        for (i = 14 + redPawnTotal; i < 14 + redPawnTotal + blackPawnTotal; ++i) {
            blackPawnRow += (9 - (tier[i] - '0') == step - 4);
        }
        return choose[9][blackPawnRow];
    }

    case 14: case 15: case 16: case 17: case 18: case 19: {
        /* Kights, cannons, and rooks can reach any slot. The number of ways
           to place k such pieces is nCr(90-existing_pieces, k). */
        int existing = 2;
        for (i = 0; i < step - 8; ++i) {
            existing += tier[i] - '0';
        }
        int target = tier[step - 8] - '0';
        return choose[90 - existing][target];
    }
    }
    /* 0 is not a valid tier size and is therefore used to indicate an error. */
    return 0ULL;
}

uint64_t tier_size(const char *tier) {
    uint64_t size = 2ULL; // Red or black's turn.
    for (int step = 0; step < 20; ++step) {
        uint64_t stepSize = tier_size_step(tier, step);
        if (size > UINT64_MAX / stepSize) {
            /* Overflow after multiplying, return error. */
            return 0ULL;
        }
        size *= stepSize;
    }
    return size;
}

static void tally(char *tier) {
    uint64_t size = tier_size(tier);
    /* Start counter at 1 since 0ULL is reserved for error.
       When calculations are done, an error has occurred
       if this value is 0ULL. Otherwise, decrement this counter
       to get the actual value. */
    uint64_t childSizeTotal = 1ULL;
    uint64_t childSizeMax = 0ULL;
    uint64_t mem;
    bool overflow = false;
    bool feasible = false;

    TierList *childTiers = child_tiers(tier);
    printf("child tiers: ");
    for (struct TierListElem *curr = childTiers; curr; curr = curr->next) {
        printf("%s ", curr->tier);
        uint64_t currChildSize = tier_size(curr->tier);
        childSizeTotal = safe_add_uint64(childSizeTotal, currChildSize);
        if (currChildSize > childSizeMax) {
            childSizeMax = currChildSize;
        }
    }
    printf("\n");
    free_tier_list(childTiers);

    if (size == 0ULL || childSizeTotal == 0ULL) {
        overflow = true;
        ++tierCountIgnored;
    } else {
        /* memory needed = 16*childSizeTotal + 19*size. */
        mem = safe_add_uint64(
                    safe_mult_uint64(19ULL, size),
                    safe_mult_uint64(16ULL, childSizeTotal)
                    );
        if (mem == 0ULL) {
            overflow = true;
            ++tierCountIgnored;
            /* We initialized childSizeTotal to 1, so we need to fix the calculation. */
        } else if ( (mem -= 16ULL) < (96ULL*(1ULL << 30)) ) { // fits in 96 GiB
            ++tierCount96GiB;
            feasible = true;
        } else if ( mem < (384ULL*(1ULL << 30)) ) { // fits in 384 GiB
            ++tierCount384GiB;
            feasible = true;
        } else if ( mem < (1536ULL*(1ULL << 30)) ) { // fits in 1536 GiB
            ++tierCount1536GiB;
            feasible = true;
        } else {
            ++tierCountIgnored;
        }
    }

    /* Update global tier counters only if we decide to solve current tier. */
    if (feasible) {
        if (size > maxTierSize) {
            maxTierSize = size;
        }
        tierSizeTotal += size;
    }

    /* Print out details only if the list is short. */
    if (END_GAME_PIECES_MAX < 4) {
        if (overflow) {
            printf("%s: overflow\n\n", tier);
        } else {
            /* We initialized childSizeTotal to 1, so we need to decrement it. */
            printf("%s: size == %"PRIu64", max child tier size == %"PRIu64","
                   " child tiers size total == %"PRIu64", MEM == %"PRIu64"B\n\n",
                   tier, size, childSizeMax, --childSizeTotal, mem);
        }
    } else {
        (void)overflow; // suppress unused variable warning.
    }
}

static void append_black_pawns(char *tier) {
    int begin = 14 + tier[RED_P_IDX] - '0';
    int nump = tier[BLACK_P_IDX] - '0';
    tier[begin - 1] = '_';
    for (int i = 0; i < nump; ++i) {
        tier[begin + i] = '0';
    }
    tier[begin + nump] = '\0';
    while (true) {
        tally(tier);
        /* Go to next combination. */
        int i = begin;
        ++tier[begin];
        while (tier[i] > '6' && i < begin + nump) {
            ++tier[++i];
        }
        if (i == begin + nump) {
            break;
        }
        for (int j = begin; j < i; ++j) {
            tier[j] = tier[i];
        }
    }
}

static void append_red_pawns(char *tier) {
    tier[12] = '_';
    int numP = tier[RED_P_IDX] - '0';
    for (int i = 0; i < numP; ++i) {
        tier[13 + i] = '0';
    }
    while (true) {
        append_black_pawns(tier);
        /* Go to next combination. */
        int i = 13;
        ++tier[13];
        while (tier[i] > '6' && i < 13 + numP) {
            ++tier[++i];
        }
        if (i == 13 + numP) {
            break;
        }
        for (int j = 13; j < i; ++j) {
            tier[j] = tier[i];
        }
    }
}

static void generate_tiers(char *tier) {
    /* Do not include tiers that exceed maximum
       number of pieces on board. */
    int count = 0;
    for (int i = 0; i < 12; ++i) {
        count += tier[i] - '0';
    }
    /* Do not consider tiers that have more pieces
       than allowed on the board. */
    if (count > END_GAME_PIECES_MAX) return;
    append_red_pawns(tier);
}

static void next_rem(char *tier) {
    int i = 0;
    ++tier[0];
    while (tier[i] > REM_MAX[i]) {
        /* Carry. */
        tier[i++] = '0';
        if (i == 12) break;
        ++tier[i];
    }
}

void tier_driver(void) {
    make_triangle();
    char tier[TIER_STR_LENGTH_MAX] = "000000000000"; // 12 digits.
    // 3^10 * 6^2 = 2125764 possible sets of remaining pieces on the board.
    for (int i = 0; i < 2125764; ++i) {
        generate_tiers(tier);
        next_rem(tier);
    }
    printf("total solvable tiers with a maximum of %d pieces: %"PRIu64"\n",
           END_GAME_PIECES_MAX, tierCount96GiB+tierCount384GiB+tierCount1536GiB);
    printf("number of tiers that fit in 96 GiB memory: %"PRIu64"\n", tierCount96GiB);
    printf("number of tiers that fit in 384 GiB memory: %"PRIu64"\n", tierCount384GiB);
    printf("number of tiers that fit in 1536 GiB memory: %"PRIu64"\n", tierCount1536GiB);
    printf("number of tiers ignored: %"PRIu64"\n", tierCountIgnored);
    printf("max solvable tier size: %"PRIu64"\n", maxTierSize);
    /* Although in theory, this tierSizeTotal value could overflow, but it's unlikely.
       Not checking for now. */
    printf("total size of all solvable tiers: %"PRIu64"\n", tierSizeTotal);
}

/* ------------------------------- Multithreaded Implementation -------------------------------------*/

static void tally_multithread(char *tier, tier_stat_t *stat) {
    uint64_t size = tier_size(tier);
    /* Start counter at 1 since 0ULL is reserved for error.
       When calculations are done, an error has occurred
       if this value is 0ULL. Otherwise, decrement this counter
       to get the actual value. */
    uint64_t childSizeTotal = 1ULL;
    uint64_t childSizeMax = 0ULL;
    uint64_t mem;
    bool overflow = false;
    bool feasible = false;

    TierList *childTiers = child_tiers(tier);
    for (struct TierListElem *curr = childTiers; curr; curr = curr->next) {
        uint64_t currChildSize = tier_size(curr->tier);
        childSizeTotal = safe_add_uint64(childSizeTotal, currChildSize);
        if (currChildSize > childSizeMax) {
            childSizeMax = currChildSize;
        }
    }
    free_tier_list(childTiers);

    if (size == 0ULL || childSizeTotal == 0ULL) {
        overflow = true;
        ++stat->tierCountIgnored;
    } else {
        /* memory needed = 16*childSizeTotal + 19*size. */
        mem = safe_add_uint64(
                    safe_mult_uint64(19ULL, size),
                    safe_mult_uint64(16ULL, childSizeTotal)
                    );
        if (mem == 0ULL) {
            overflow = true;
            ++stat->tierCountIgnored;
            /* We initialized childSizeTotal to 1, so we need to fix the calculation. */
        } else if ( (mem -= 16ULL) < (96ULL*(1ULL << 30)) ) { // fits in 96 GiB
            ++stat->tierCount96GiB;
            feasible = true;
        } else if ( mem < (384ULL*(1ULL << 30)) ) { // fits in 384 GiB
            ++stat->tierCount384GiB;
            feasible = true;
        } else if ( mem < (1536ULL*(1ULL << 30)) ) { // fits in 1536 GiB
            ++stat->tierCount1536GiB;
            feasible = true;
        } else {
            ++stat->tierCountIgnored;
        }
    }

    /* Update global tier counters only if we decide to solve current tier. */
    if (feasible) {
        if (size > stat->maxTierSize) {
            stat->maxTierSize = size;
        }
        stat->tierSizeTotal += size;
    }

    /* Print out details only if the list is short. */
    if (END_GAME_PIECES_MAX < 4) {
        if (overflow) {
            printf("%s: overflow\n", tier);
        } else {
            /* We initialized childSizeTotal to 1, so we need to decrement it. */
            printf("%s: size == %"PRIu64", max child tier size == %"PRIu64","
                   " child tiers size total == %"PRIu64", MEM == %"PRIu64"B\n",
                   tier, size, childSizeMax, --childSizeTotal, mem);
        }
    } else {
        (void)overflow; // suppress unused variable warning.
    }
}

static void append_black_pawns_multithread(char *tier, tier_stat_t *stat) {
    int begin = 14 + tier[RED_P_IDX] - '0';
    int nump = tier[BLACK_P_IDX] - '0';
    tier[begin - 1] = '_';
    for (int i = 0; i < nump; ++i) {
        tier[begin + i] = '0';
    }
    tier[begin + nump] = '\0';
    while (true) {
        tally_multithread(tier, stat);
        /* Go to next combination. */
        int i = begin;
        ++tier[begin];
        while (tier[i] > '6' && i < begin + nump) {
            ++tier[++i];
        }
        if (i == begin + nump) {
            break;
        }
        for (int j = begin; j < i; ++j) {
            tier[j] = tier[i];
        }
    }
}

static void append_red_pawns_multithread(char *tier, tier_stat_t *stat) {
    tier[12] = '_';
    int numP = tier[RED_P_IDX] - '0';
    for (int i = 0; i < numP; ++i) {
        tier[13 + i] = '0';
    }
    while (true) {
        append_black_pawns_multithread(tier, stat);
        /* Go to next combination. */
        int i = 13;
        ++tier[13];
        while (tier[i] > '6' && i < 13 + numP) {
            ++tier[++i];
        }
        if (i == 13 + numP) {
            break;
        }
        for (int j = 13; j < i; ++j) {
            tier[j] = tier[i];
        }
    }
}

static void generate_tiers_multithread(char *tier, tier_stat_t *stat) {
    /* Do not include tiers that exceed maximum
       number of pieces on board. */
    int count = 0;
    for (int i = 0; i < 12; ++i) {
        count += tier[i] - '0';
    }
    /* Do not consider tiers that have more pieces
       than allowed on the board. */
    if (count > END_GAME_PIECES_MAX) return;
    append_red_pawns_multithread(tier, stat);
}

typedef struct TDMHelperArgs {
    uint64_t begin;
    uint64_t end;
    char **tiers;
} tdm_helper_args_t;

static void *tdm_helper(void *_args) {
    tdm_helper_args_t *args = (tdm_helper_args_t*)_args;
    tier_stat_t *stat = (tier_stat_t*)calloc(1, sizeof(tier_stat_t));
    for (uint64_t i = args->begin; i < args->end; ++i) {
        generate_tiers_multithread(args->tiers[i], stat);
    }
    pthread_exit(stat);
    return NULL;
}

void tier_driver_multithread(uint64_t nthread) {
    make_triangle();
    char tier[TIER_STR_LENGTH_MAX] = "000000000000";
    uint64_t num_tiers = 2125764ULL;
    char **tiers = (char**)safe_calloc(num_tiers, sizeof(char*));
    for (uint64_t i = 0; i < num_tiers; ++i) {
        tiers[i] = (char*)safe_malloc(TIER_STR_LENGTH_MAX);
        for (int j = 0; j < TIER_STR_LENGTH_MAX; ++j) {
            tiers[i][j] = tier[j];
        }
        next_rem(tier);
    }

    pthread_t *tid = (pthread_t*)safe_calloc(nthread, sizeof(pthread_t*));
    tdm_helper_args_t *args = (tdm_helper_args_t*)safe_calloc(nthread, sizeof(tdm_helper_args_t));

    for (uint64_t i = 0; i < nthread; ++i) {
        args[i].begin = i * (num_tiers / nthread);
        args[i].end = (i == nthread - 1) ? num_tiers : (i + 1) * (num_tiers / nthread);
        args[i].tiers = tiers;
        pthread_create(tid + i, NULL, tdm_helper, (void*)(args + i));
    }
    for (uint64_t i = 0; i < nthread; ++i) {
        tier_stat_t *stat;
        pthread_join(tid[i], (void**)&stat);
        /* Update global statistics. */
        tierCount1536GiB += stat->tierCount1536GiB;
        tierCount384GiB += stat->tierCount384GiB;
        tierCount96GiB += stat->tierCount96GiB;
        tierCountIgnored += stat->tierCountIgnored;
        tierSizeTotal += stat->tierSizeTotal;
        if (stat->maxTierSize > maxTierSize) {
            maxTierSize = stat->maxTierSize;
        }
        free(stat);
    }
    free(args);
    free(tid);
    for (uint64_t i = 0; i < num_tiers; ++i) {
        free(tiers[i]);
    }
    free(tiers);

    printf("total solvable tiers with a maximum of %d pieces: %"PRIu64"\n",
           END_GAME_PIECES_MAX, tierCount96GiB+tierCount384GiB+tierCount1536GiB);
    printf("number of tiers that fit in 96 GiB memory: %"PRIu64"\n", tierCount96GiB);
    printf("number of tiers that fit in 384 GiB memory: %"PRIu64"\n", tierCount384GiB);
    printf("number of tiers that fit in 1536 GiB memory: %"PRIu64"\n", tierCount1536GiB);
    printf("number of tiers ignored: %"PRIu64"\n", tierCountIgnored);
    printf("max solvable tier size: %"PRIu64"\n", maxTierSize);
    /* Although in theory, this tierSizeTotal value could overflow, but it's unlikely.
       Not checking for now. */
    printf("total size of all solvable tiers: %"PRIu64"\n", tierSizeTotal);
}

























