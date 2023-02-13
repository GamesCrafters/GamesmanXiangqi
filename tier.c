#include "tier.h"
#include "types.h"
#include "misc.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

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
 * BLACK_PAWN_ROWS has the exact same format as RED_PAWN_ROWS
 * except that we start counting the row number from the bottom
 * row of red's side.
 */

/* There are only 90 slots on a Chinese chess board;
   maximum number of a type of pieces is 5, so we
   only need n choose k<=5 values. */
#define CHOOSE_ROWS 91
#define CHOOSE_COLS 6
static uint64_t choose[CHOOSE_ROWS][CHOOSE_COLS];
static bool chooseInitialized = false;

/********************* Helper Function Declarations *********************/
static void make_triangle(void);
static uint64_t safe_add_uint64(uint64_t lhs, uint64_t rhs);
static uint64_t safe_mult_uint64(uint64_t lhs, uint64_t rhs);

static TierList *rm_insert(TierList *list, char *tier, int idx);
static TierList *rm_pawn_insert(TierList *list, char *tier, int idx, int row);
static TierList *rm_pfwd_insert(TierList *list, char *tier, int pieceIdx,
                                int pawnIdx, int pawnRow);
static TierList *rm_pawn_pfwd_insert(TierList *list, char *tier, int capturedIdx,
                                     int capturedRow, int fwdIdx, int fwdRow);
/******************* End Helper Function Declarations *******************/

TierList *tier_list_insert_head(TierList *list, const char *tier) {
    TierList *newHead = (TierList *)safe_malloc(sizeof(struct TierListElem));
    newHead->next = list;
    for (int i = 0; i < TIER_STR_LENGTH_MAX; ++i) {
        newHead->tier[i] = tier[i];
    }
    return newHead;
}

void tier_list_destroy(TierList *list) {
    struct TierListElem *next;
    while (list) {
        next = list->next;
        free(list);
        list = next;
    }
}

void tier_array_destroy(struct TierArray *array) {
    if (!array || !array->tiers) return;
    for (uint8_t i = 0; i < array->size; ++i) {
        free(array->tiers[i]);
    }
    free(array->tiers);
    array->tiers = NULL;
}

static void find_pawn_locations(const char *tier, bool *redPRow, bool *blackPRow) {
    int redPawnCount = tier[RED_P_IDX] - '0';
    int blackPawnCount = tier[BLACK_P_IDX] - '0';
    int i;
    for (i = 13; i < 13 + redPawnCount; ++i) {
        redPRow[tier[i] - '0'] = true;
    }
    for (i = 14+redPawnCount; i < 14+redPawnCount+blackPawnCount; ++i) {
        blackPRow[tier[i] - '0'] = true;
    }
}

/**
 * @brief Returns a linked list of child tiers of the given TIER.
 */
TierList *tier_get_child_tier_list(const char *tier) {
    TierList *list = NULL;
    bool redPRow[7] = {false};
    bool blackPRow[7] = {false};
    bool redHasA = tier[RED_A_IDX] > '0';
    bool redHasB = tier[RED_B_IDX] > '0';
    bool redHasN = tier[RED_N_IDX] > '0';
    bool redHasC = tier[RED_C_IDX] > '0';
    bool redHasR = tier[RED_R_IDX] > '0';
    bool blackHasA = tier[BLACK_A_IDX] > '0';
    bool blackHasB = tier[BLACK_B_IDX] > '0';
    bool blackHasN = tier[BLACK_N_IDX] > '0';
    bool blackHasC = tier[BLACK_C_IDX] > '0';
    bool blackHasR = tier[BLACK_R_IDX] > '0';
    bool redHasRCN = redHasR || redHasC || redHasN;
    bool blackHasRCN = blackHasR || blackHasC || blackHasN;
    char tierCpy[TIER_STR_LENGTH_MAX];
    int redPawnCount = tier[RED_P_IDX] - '0';
    int blackPawnCount = tier[BLACK_P_IDX] - '0';
    int i;
    for (i = 0; i < TIER_STR_LENGTH_MAX; ++i) {
        tierCpy[i] = tier[i];
    }
    find_pawn_locations(tier, redPRow, blackPRow);

    /* 1. CHILD TIERS BY CAPTURING. */

    /* Advisors can be captured if opponent has R/C/N, a pawn on row
       0/1/2 without moving forward, or a pawn on row 1/2/3 with a
       forward move. */
    if (redHasA) {
        if (blackHasRCN || blackPRow[0] || blackPRow[1] || blackPRow[2]) {
            list = rm_insert(list, tierCpy, RED_A_IDX);
        }
        for (i = 1; i <= 3; ++i) {
            if (blackPRow[i]) {
                list = rm_pfwd_insert(list, tierCpy, RED_A_IDX, BLACK_P_IDX, i);
            }
        }
    }
    if (blackHasA) {
        if (redHasRCN || redPRow[0] || redPRow[1] || redPRow[2]) {
            list = rm_insert(list, tierCpy, BLACK_A_IDX);
        }
        for (i = 1; i <= 3; ++i) {
            if (redPRow[i]) {
                list = rm_pfwd_insert(list, tierCpy, BLACK_A_IDX, RED_P_IDX, i);
            }
        }
    }

    /* Bishops can be captured if opponent has R/C/N, a pawn on row
       0/2/4 without moving forward, or a pawn on row 1/3/5 with a
       forward move. */
    if (redHasB) {
        if (blackHasRCN || blackPRow[0] || blackPRow[2] || blackPRow[4]) {
            list = rm_insert(list, tierCpy, RED_B_IDX);
        }
        for (i = 1; i <= 5; i += 2) {
            if (blackPRow[i]) {
                list = rm_pfwd_insert(list, tierCpy, RED_B_IDX, BLACK_P_IDX, i);
            }
        }
    }
    if (blackHasB) {
        if (redHasRCN || redPRow[0] || redPRow[2] || redPRow[4]) {
            list = rm_insert(list, tierCpy, BLACK_B_IDX);
        }
        for (i = 1; i <= 5; i += 2) {
            if (redPRow[i]) {
                list = rm_pfwd_insert(list, tierCpy, BLACK_B_IDX, RED_P_IDX, i);
            }
        }
    }

    /* A pawn on row 0/1/2 can always be captured by the opponent king,
       but cannot be captured by an opponent pawn. */
    for (i = 0; i < 3; ++i) {
        if (redPRow[i]) {
            list = rm_pawn_insert(list, tierCpy, RED_P_IDX, i);
        }
        if (blackPRow[i]) {
            list = rm_pawn_insert(list, tierCpy, BLACK_P_IDX, i);
        }
    }

    /* A pawn on row 3 can be captured only if opponent has R/C/N,
       and cannot be captured by an opponent pawn. */
    if (redPRow[3] && blackHasRCN) {
        list = rm_pawn_insert(list, tierCpy, RED_P_IDX, 3);
    }
    if (blackPRow[3] && redHasRCN) {
        list = rm_pawn_insert(list, tierCpy, BLACK_P_IDX, 3);
    }

    /* A pawn on row 4 can be captured if opponent has R/C/N/B, or a
       pawn on row 6 with a forward move (3 code blocks down). */
    if (redPRow[4] && (blackHasRCN || blackHasB)) {
        list = rm_pawn_insert(list, tierCpy, RED_P_IDX, 4);
    }
    if (blackPRow[4] && (redHasRCN || redHasB)) {
        list = rm_pawn_insert(list, tierCpy, BLACK_P_IDX, 4);
    }

    /* A pawn on row 5 can be captured if opponent has R/C/N, a pawn
       on row 4, or a pawn on row 5 with a forward move (2 code blocks
       down). */
    if (redPRow[5] && (blackHasRCN || blackPRow[4])) {
        list = rm_pawn_insert(list, tierCpy, RED_P_IDX, 5);
    }
    if (blackPRow[5] && (redHasRCN || redPRow[4])) {
        list = rm_pawn_insert(list, tierCpy, BLACK_P_IDX, 5);
    }

    /* A pawn on row 6 can be captured if opponent has R/C/N, a pawn
       on row 3, or a pawn on row 4 with a forward move (after this
       code block).*/
    if (redPRow[6] && (blackHasRCN || blackPRow[3])) {
        list = rm_pawn_insert(list, tierCpy, RED_P_IDX, 6);
    }
    if (blackPRow[6] && (redHasRCN || redPRow[3])) {
        list = rm_pawn_insert(list, tierCpy, BLACK_P_IDX, 6);
    }

    /* Pawn captures with a forward pawn move. */
    for (i = 4; i <= 6; ++i) {
        if (redPRow[i] && blackPRow[10-i]) {
            list = rm_pawn_pfwd_insert(list, tierCpy, RED_P_IDX, i, BLACK_P_IDX, 10-i);
            list = rm_pawn_pfwd_insert(list, tierCpy, BLACK_P_IDX, 10-i, RED_P_IDX, i);
        }
    }

    /* Knights, cannons, and rooks can always be captured by the oppoent
       king, or an opponent pawn not on row 0 with a forward move. */
    for (i = RED_N_IDX; i <= BLACK_R_IDX; ++i) {
        if (tier[i] > '0') {
            list = rm_insert(list, tierCpy, i);
            for (int j = 1; j <= 6; ++j) {
                if (!(i & 1) && blackPRow[j]) {
                    list = rm_pfwd_insert(list, tierCpy, i, BLACK_P_IDX, j);
                } else if ((i & 1) && redPRow[j]) {
                    list = rm_pfwd_insert(list, tierCpy, i, RED_P_IDX, j);
                }
            }
        }
    }

    /* 2. CHILD TIERS BY A FORWARD PAWN MOVE W/O CAPTURING.
       Note that we ignored the possible cases where a forward pawn move
       is not available without capturing an opponent pawn. This will
       waste some memory during the solving phase but is not a bug. */

    /* Check all available forward red pawn moves. Note that the row
       numbers of pawns are sorted in descending order, so we can
       stop the loop once we see a pawn on the 0th row, which is the
       bottom-most row on the opponent's side. */
    for (i = 13; i < 13 + redPawnCount && tier[i] > '0'; ++i) {
        /* Skip current pawn if next pawn is also on the same row
           because moving either pawn gives the same tier. No need
           to check if i+1 is out of bounds. */
        while (tier[i] == tier[i+1]) ++i;
        --tierCpy[i];
        list = tier_list_insert_head(list, tierCpy);
        ++tierCpy[i];
    }

    /* Check forward black pawn moves. */
    for (i = 14+redPawnCount; i < 14+redPawnCount+blackPawnCount && tier[i] > '0'; ++i) {
        while (tier[i] == tier[i+1]) ++i;
        --tierCpy[i];
        list = tier_list_insert_head(list, tierCpy);
        ++tierCpy[i];
    }
    return list;
}

/**
 * @brief Returns a dynamic array of child tiers of TIER.
 * The array should be freed by the caller of this function.
 */
struct TierArray tier_get_child_tier_array(const char *tier) {
    struct TierArray array;
    TierList *list = tier_get_child_tier_list(tier);
    struct TierListElem *walker;
    uint8_t i;

    /* Count the number of child tiers in the list. */
    array.size = 0;
    for (walker = list; walker; walker = walker->next) {
        ++array.size;
    }

    /* Allocate space. */
    array.tiers = (char**)safe_malloc(array.size * sizeof(char*));
    for (i = 0; i < array.size; ++i) {
        array.tiers[i] = safe_malloc(TIER_STR_LENGTH_MAX * sizeof(char));
    }

    /* Copy tier strings. */
    for (walker = list, i = 0; walker; walker = walker->next, ++i) {
        for (int j = 0; j < TIER_STR_LENGTH_MAX; ++j) {
            array.tiers[i][j] = walker->tier[j];
        }
    }
    tier_list_destroy(list);
    return array;
}

/**
 * @brief Returns the number of child tiers of TIER.
 */
uint8_t tier_num_child_tiers(const char *tier) {
    TierList *list = tier_get_child_tier_list(tier);
    uint8_t n = 0;
    TierList *walker = list;
    while (walker) {
        ++n;
        walker = walker->next;
    }
    tier_list_destroy(list);
    return n;
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
    int redPawnCount = tier[RED_P_IDX] - '0';
    int blackPawnCount = tier[BLACK_P_IDX] - '0';
    int redPawnRow = 0, blackPawnRow = 0;
    int i;
    if (!chooseInitialized){
        make_triangle();
        chooseInitialized = true;
    }
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
        for (i = 13; i < 13 + redPawnCount; ++i) {
            redPawnRow += (tier[i] - '0' == step - 4);
        }
        return choose[9][redPawnRow];
    }

    case 7: case 8: case 9: case 10: {
        for (i = 13; i < 13 + redPawnCount; ++i) {
            redPawnRow += (tier[i] - '0' == step - 4);
        }
        for (i = 14 + redPawnCount; i < 14 + redPawnCount + blackPawnCount; ++i) {
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
        for (i = 14 + redPawnCount; i < 14 + redPawnCount + blackPawnCount; ++i) {
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

uint64_t tier_required_mem(const char *tier) {
    uint64_t size = tier_size(tier);
    if (!size) {
        return 0ULL;
    }
    /* Start counter at 1 since 0ULL is reserved for error.
       When calculations are done, we know that an error has
       occurred if this value is 0ULL. Otherwise, decrement
       this counter to get the actual value. */
    uint64_t childSizeTotal = 1ULL;
    TierList *childTiers = tier_get_child_tier_list(tier);
    for (struct TierListElem *curr = childTiers; curr; curr = curr->next) {
        uint64_t currChildSize = tier_size(curr->tier);
        childSizeTotal = safe_add_uint64(childSizeTotal, currChildSize);
    }
    tier_list_destroy(childTiers);

    if (!childSizeTotal) {
        return 0ULL;
    }
    uint64_t mem = safe_add_uint64(
                safe_mult_uint64(19ULL, size),
                safe_mult_uint64(16ULL, childSizeTotal)
                );
    if (!mem) {
        return 0ULL;
    }
    /* We initialized childSizeTotal to 1, so we need to fix the calculation. */
    return mem - 16ULL;
}

/***************************** Helper Functions ******************************/

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

static void get_pawn_begin_end(const char *tier, int pawnIdx, int *begin, int *end) {
    int redPawnCount = tier[RED_P_IDX] - '0';
    int blackPawnCount = tier[BLACK_P_IDX] - '0';
    if (pawnIdx == RED_P_IDX) {
        *begin = 13;
        *end = 13 + redPawnCount;
    } else {
        *begin = 14+redPawnCount;
        *end = 14+redPawnCount+blackPawnCount;
    }
}

static void get_pawn_rbegin_rend(const char *tier, int pawnIdx, int *rbegin, int *rend) {
    int redPawnCount = tier[RED_P_IDX] - '0';
    int blackPawnCount = tier[BLACK_P_IDX] - '0';
    if (pawnIdx == RED_P_IDX) {
        *rbegin = 12 + redPawnCount;
        *rend = 12;
    } else {
        *rbegin = 13+redPawnCount+blackPawnCount;
        *rend = 13+redPawnCount;
    }
}

static void add_pawn(char *tier, int pawnIdx, int row) {
    assert(tier[pawnIdx] <= '5');
    int begin, end, i;
    get_pawn_begin_end(tier, pawnIdx, &begin, &end);
    ++tier[pawnIdx];
    for (i = begin; i < end && tier[i] > '0'+row; ++i);
    str_insert(tier, '0'+row, i);
}

static void rm_pawn(char *tier, int pawnIdx, int row) {
    assert(tier[pawnIdx] > '0');
    int begin, end, i;
    get_pawn_begin_end(tier, pawnIdx, &begin, &end);
    --tier[pawnIdx];
    for (i = begin; tier[i] != '0'+row; ++i);
    assert(i < end);
    str_shift_left(tier, i);
}

static void move_pawn_forward(char *tier, int pawnIdx, int row) {
    assert(tier[pawnIdx] > '0');
    int begin, end, i;
    get_pawn_rbegin_rend(tier, pawnIdx, &begin, &end);
    // TODO
}

static void move_pawn_backward(char *tier, int idx, int row) {
    // TODO
}

static TierList *rm_insert(TierList *list, char *tier, int idx) {
    --tier[idx];
    list = tier_list_insert_head(list, tier);
    ++tier[idx];
    return list;
}

static TierList *rm_pawn_insert(TierList *list, char *tier, int idx, int row) {
    rm_pawn(tier, idx, row);
    list = tier_list_insert_head(list, tier);
    add_pawn(tier, idx, row);
    return list;
}

static TierList *rm_pfwd_insert(TierList *list, char *tier, int pieceIdx,
                                int pawnIdx, int pawnRow) {
    --tier[pieceIdx];
    move_pawn_forward(tier, pawnIdx, pawnRow);
    list = tier_list_insert_head(list, tier);
    move_pawn_backward(tier, pawnIdx, pawnRow);
    ++tier[pieceIdx];
    return list;
}

static TierList *rm_pawn_pfwd_insert(TierList *list, char *tier, int capturedIdx,
                                     int capturedRow, int fwdIdx, int fwdRow) {
    move_pawn_forward(tier, fwdIdx, fwdRow);
    list = rm_pawn_insert(list, tier, capturedIdx, capturedRow);
    move_pawn_backward(tier, fwdIdx, fwdRow);
    return list;
}

/*************************** END Helper Functions ****************************/
