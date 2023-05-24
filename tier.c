#include "tier.h"
#include "common.h"
#include "misc.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

/* Max number of remaining pieces of each type. */
static const char *REM_MAX = "222255222222";

/********************* Helper Function Declarations *********************/
static uint64_t safe_add_uint64(uint64_t lhs, uint64_t rhs);
static uint64_t safe_mult_uint64(uint64_t lhs, uint64_t rhs);

static void get_pawn_begin_end(const char *tier, int8_t pawnIdx, int *begin, int *end);
static void get_pawn_rbegin_rend(const char *tier, int8_t pawnIdx, int *rbegin, int *rend);

static TierList *rm_insert(TierList *list, char *tier, int8_t idx);
static TierList *rm_pawn_insert(TierList *list, char *tier, int8_t idx, int8_t row);
static TierList *rm_pfwd_insert(TierList *list, char *tier, int8_t pieceIdx,
                                int8_t pawnIdx, int8_t pawnRow);
static TierList *rm_pawn_pfwd_insert(TierList *list, char *tier, int8_t capturedIdx,
                                     int8_t capturedRow, int8_t fwdIdx, int8_t fwdRow);
static TierList *add_insert(TierList *list, char *tier, int8_t idx);
static TierList *add_pawn_insert(TierList *list, char *tier, int8_t idx, int8_t row);
static TierList *add_pbwd_insert(TierList *list, char *tier, int8_t pieceIdx,
                                 int8_t pawnIdx, int8_t pawnRow);
static TierList *add_pawn_pbwd_insert(TierList *list, char *tier, int8_t addIdx,
                                      int8_t addRow, int8_t bwdIdx, int8_t bwdRow);
/******************* End Helper Function Declarations *******************/

/**************************** Tier Utilities ****************************/

/**
 * @brief Returns true if TIER is legal, false otherwise. 
 * A legal tier must satisfies the following conditions:
 * 
 * 1. Each tier[i] for 0 <= i < 12 must be a digit between '0'
 * and REM_MAX[i], both inclusive.
 * 
 * 2. Character tier[12] must be '_'.
 * 
 * 3. Each tier[i] for 13 <= i < 13+numP must be a digit between
 * '0' and '6', both inclusive.
 * 
 * 4. Character tier[13+numP] must be '_'.
 * 
 * 5. Each tier[i] for 14+numP <= i < 14+numP+nump must be a digit
 * between '0' and '6', both inclusive.
 * 
 * 6. Character tier[14+numP+nump] must be the null terminator '\0'.
 * 
 * 7. If both sides have 5 pawns, they must not be on the same row.
 */
bool tier_is_legal_tier(const char *tier) {
    int i, begin, end;

    /* Validate piece configuration. */
    for (i = 0; i < 12; ++i) {
        if (tier[i] < '0' || tier[i] > REM_MAX[i]) return false;
    }
    if (tier[12] != '_') return false;
    for (int parity = 0; parity < 2; ++parity) {
        get_pawn_begin_end(tier, RED_P_IDX + parity, &begin, &end);
        for (i = begin; i < end; ++i) {
            if (tier[i] < '0' || tier[i] > '6') return false;
        }
        if (!parity && tier[i] != '_') return false;
        if (parity && tier[i] != '\0') return false;
    }

    /* Validate pawns. */
    if (tier[RED_P_IDX] != '5' || tier[BLACK_P_IDX] != '5') return true;
    char hold = tier[13];
    for (i = 14; i < 18; ++i) if (tier[i] != hold) return true;
    for (i = 19; i < 24; ++i) if (tier[i] != 9 - hold + '0' + '0') return true;
    return false;
}

struct TierListElem *tier_get_canonical_tier(const char *tier) {
    int i, j, begin, end;
    struct TierListElem *e = calloc(1, sizeof(struct TierListElem));
    if (!e) return NULL;
    
    /* Swap piece colors. */
    for (i = 0; i < 12; ++i) {
        e->tier[i] = tier[i ^ 1];
    }

    /* Swap pawns. */
    get_pawn_begin_end(tier, BLACK_P_IDX, &begin, &end);
    e->tier[i++] = '_';
    for (j = begin; j < end; ++j) {
        e->tier[i++] = tier[j];
    }
    get_pawn_begin_end(tier, RED_P_IDX, &begin, &end);
    e->tier[i++] = '_';
    for (j = begin; j < end; ++j) {
        e->tier[i++] = tier[j];
    }
    /* If new tier is not the canonical one, return TIER instead of the new tier. */
    if (strncmp(tier, e->tier, TIER_STR_LENGTH_MAX) > 0) {
        memcpy(e->tier, tier, TIER_STR_LENGTH_MAX);
    }
    return e;
}

bool tier_is_canonical_tier(const char *tier) {
    struct TierListElem *e = tier_get_canonical_tier(tier);
    bool equal = !(strncmp(e->tier, tier, TIER_STR_LENGTH_MAX));
    free(e);
    return equal;
}

static TierList *tier_list_insert_head(TierList *list, const char *tier, tier_change_t change) {
    TierList *newHead = (TierList*)safe_malloc(sizeof(struct TierListElem));
    newHead->next = list;
    memcpy(newHead->tier, tier, TIER_STR_LENGTH_MAX);
    newHead->change = change;
    return newHead;
}

static void find_pawn_locations(const char *tier, bool *redPRow, bool *blackPRow) {
    int i, begin, end;

    get_pawn_begin_end(tier, RED_P_IDX, &begin, &end);
    for (i = begin; i < end; ++i) {
        redPRow[tier[i] - '0'] = true;
    }

    get_pawn_begin_end(tier, BLACK_P_IDX, &begin, &end);
    for (i = begin; i < end; ++i) {
        blackPRow[tier[i] - '0'] = true;
    }
}

struct TierProperties {
    bool redPRow[7];
    bool blackPRow[7];
    bool exists[12];
    bool missing[12];
    bool redHasRCN, redHasRCNB;
    bool blackHasRCN, blackHasRCNB;
};

static struct TierProperties get_tier_properties(const char *tier) {
    struct TierProperties p;
    memset(&p, 0, sizeof(struct TierProperties));

    for (int i = 0; i < 12; ++i) {
        p.exists[i] = tier[i] > '0';
        p.missing[i] = tier[i] < REM_MAX[i];
    }

    p.redHasRCN = p.exists[RED_R_IDX] || p.exists[RED_C_IDX] || p.exists[RED_N_IDX];
    p.redHasRCNB = p.redHasRCN || p.exists[RED_B_IDX];
    p.blackHasRCN = p.exists[BLACK_R_IDX] || p.exists[BLACK_C_IDX] || p.exists[BLACK_N_IDX];
    p.blackHasRCNB = p.blackHasRCN || p.exists[RED_B_IDX];

    find_pawn_locations(tier, p.redPRow, p.blackPRow);
    return p;
}

/**
 * @brief Returns a linked list of child tiers of the given TIER.
 * Assumes TIER is legal.
 */
TierList *tier_get_child_tier_list(const char *tier) {
    TierList *list = NULL;
    struct TierProperties p = get_tier_properties(tier);
    tier_change_t change;
    int i, j, begin, end;
    char tierCpy[TIER_STR_LENGTH_MAX];
    memcpy(tierCpy, tier, TIER_STR_LENGTH_MAX);

    /* 1. CHILD TIERS BY CAPTURING. */

    /* Advisors can be captured if opponent has R/C/N, a pawn on row
       0/1/2 without moving forward, or a pawn on row 1/2/3 with a
       forward move. */
    if (p.exists[RED_A_IDX]) {
        if (p.blackHasRCN || p.blackPRow[0] || p.blackPRow[1] || p.blackPRow[2]) {
            list = rm_insert(list, tierCpy, RED_A_IDX);
        }
        for (i = 1; i <= 3; ++i) {
            if (p.blackPRow[i]) {
                list = rm_pfwd_insert(list, tierCpy, RED_A_IDX, BLACK_P_IDX, i);
            }
        }
    }
    if (p.exists[BLACK_A_IDX]) {
        if (p.redHasRCN || p.redPRow[0] || p.redPRow[1] || p.redPRow[2]) {
            list = rm_insert(list, tierCpy, BLACK_A_IDX);
        }
        for (i = 1; i <= 3; ++i) {
            if (p.redPRow[i]) {
                list = rm_pfwd_insert(list, tierCpy, BLACK_A_IDX, RED_P_IDX, i);
            }
        }
    }

    /* Bishops can be captured if opponent has R/C/N, a pawn on row
       0/2/4 without moving forward, or a pawn on row 1/3/5 with a
       forward move. */
    if (p.exists[RED_B_IDX]) {
        if (p.blackHasRCN || p.blackPRow[0] || p.blackPRow[2] || p.blackPRow[4]) {
            list = rm_insert(list, tierCpy, RED_B_IDX);
        }
        for (i = 1; i <= 5; i += 2) {
            if (p.blackPRow[i]) {
                list = rm_pfwd_insert(list, tierCpy, RED_B_IDX, BLACK_P_IDX, i);
            }
        }
    }
    if (p.exists[BLACK_B_IDX]) {
        if (p.redHasRCN || p.redPRow[0] || p.redPRow[2] || p.redPRow[4]) {
            list = rm_insert(list, tierCpy, BLACK_B_IDX);
        }
        for (i = 1; i <= 5; i += 2) {
            if (p.redPRow[i]) {
                list = rm_pfwd_insert(list, tierCpy, BLACK_B_IDX, RED_P_IDX, i);
            }
        }
    }

    /* A pawn on row 0/1/2 can always be captured by the opponent king,
       but cannot be captured by an opponent pawn. */
    for (i = 0; i < 3; ++i) {
        if (p.redPRow[i]) {
            list = rm_pawn_insert(list, tierCpy, RED_P_IDX, i);
        }
        if (p.blackPRow[i]) {
            list = rm_pawn_insert(list, tierCpy, BLACK_P_IDX, i);
        }
    }

    /* A pawn on row 3 can be captured only if opponent has R/C/N,
       and cannot be captured by an opponent pawn. */
    if (p.redPRow[3] && p.blackHasRCN) {
        list = rm_pawn_insert(list, tierCpy, RED_P_IDX, 3);
    }
    if (p.blackPRow[3] && p.redHasRCN) {
        list = rm_pawn_insert(list, tierCpy, BLACK_P_IDX, 3);
    }

    /* A pawn on row 4 can be captured if opponent has R/C/N/B, or a
       pawn on row 6 with a forward move (3 code blocks down). */
    if (p.redPRow[4] && p.blackHasRCNB) {
        list = rm_pawn_insert(list, tierCpy, RED_P_IDX, 4);
    }
    if (p.blackPRow[4] && p.redHasRCNB) {
        list = rm_pawn_insert(list, tierCpy, BLACK_P_IDX, 4);
    }

    /* A pawn on row 5 can be captured if opponent has R/C/N, a pawn
       on row 4, or a pawn on row 5 with a forward move (2 code blocks
       down). */
    if (p.redPRow[5] && (p.blackHasRCN || p.blackPRow[4])) {
        list = rm_pawn_insert(list, tierCpy, RED_P_IDX, 5);
    }
    if (p.blackPRow[5] && (p.redHasRCN || p.redPRow[4])) {
        list = rm_pawn_insert(list, tierCpy, BLACK_P_IDX, 5);
    }

    /* A pawn on row 6 can be captured if opponent has R/C/N, a pawn
       on row 3, or a pawn on row 4 with a forward move (after this
       code block).*/
    if (p.redPRow[6] && (p.blackHasRCN || p.blackPRow[3])) {
        list = rm_pawn_insert(list, tierCpy, RED_P_IDX, 6);
    }
    if (p.blackPRow[6] && (p.redHasRCN || p.redPRow[3])) {
        list = rm_pawn_insert(list, tierCpy, BLACK_P_IDX, 6);
    }

    /* Pawn captures with a forward pawn move. */
    for (i = 4; i <= 6; ++i) {
        if (p.redPRow[i] && p.blackPRow[10-i]) {
            list = rm_pawn_pfwd_insert(list, tierCpy, RED_P_IDX, i, BLACK_P_IDX, 10-i);
            list = rm_pawn_pfwd_insert(list, tierCpy, BLACK_P_IDX, 10-i, RED_P_IDX, i);
        }
    }

    /* Knights, cannons, and rooks can always be captured by the oppoent
       king, or an opponent pawn not on row 0 with a forward move. */
    for (i = RED_N_IDX; i <= BLACK_R_IDX; ++i) {
        if (p.exists[i]) {
            list = rm_insert(list, tierCpy, i);
            for (j = 1; j <= 6; ++j) {
                if (!(i & 1) && p.blackPRow[j]) {
                    list = rm_pfwd_insert(list, tierCpy, i, BLACK_P_IDX, j);
                } else if ((i & 1) && p.redPRow[j]) {
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
    get_pawn_begin_end(tier, RED_P_IDX, &begin, &end);
    change.captureIdx = INVALID_IDX;
    change.captureRow = -1; // Unused.
    change.pawnIdx = RED_P_IDX;
    for (i = begin; i < end && tier[i] > '0'; ++i) {
        /* Skip current pawn if next pawn is also on the same row
           because moving either pawn gives the same tier. No need
           to check if i+1 is out of bounds. */
        while (tier[i] == tier[i+1]) ++i;
        --tierCpy[i];
        change.pawnRow = tierCpy[i] - '0';
        if (tier_is_legal_tier(tierCpy)) list = tier_list_insert_head(list, tierCpy, change);
        ++tierCpy[i];
    }

    get_pawn_begin_end(tier, BLACK_P_IDX, &begin, &end);
    change.pawnIdx = BLACK_P_IDX;
    /* Check forward black pawn moves. */
    for (i = begin; i < end && tier[i] > '0'; ++i) {
        while (tier[i] == tier[i+1]) ++i;
        --tierCpy[i];
        change.pawnRow = tierCpy[i] - '0';
        if (tier_is_legal_tier(tierCpy)) list = tier_list_insert_head(list, tierCpy, change);
        ++tierCpy[i];
    }
    return list;
}

TierList *tier_get_parent_tier_list(const char *tier) {
    TierList *list = NULL;
    struct TierProperties p = get_tier_properties(tier);
    static tier_change_t change; // Placeholder, not accessed.
    char tierCpy[TIER_STR_LENGTH_MAX];
    int i, j, rbegin, rend;
    for (i = 0; i < TIER_STR_LENGTH_MAX; ++i) {
        tierCpy[i] = tier[i];
    }

    /* 1. PARENT TIERS BY REVERSE CAPTURING. */

    /* Advisors. */
    if (p.missing[RED_A_IDX]) {
        if (p.blackHasRCN || p.blackPRow[0] || p.blackPRow[1] || p.blackPRow[2]) {
            list = add_insert(list, tierCpy, RED_A_IDX);
        }
        for (i = 0; i <= 2; ++i) {
            if (p.blackPRow[i]) {
                list = add_pbwd_insert(list, tierCpy, RED_A_IDX, BLACK_P_IDX, i);
            }
        }
    }
    if (p.missing[BLACK_A_IDX]) {
        if (p.redHasRCN || p.redPRow[0] || p.redPRow[1] || p.redPRow[2]) {
            list = add_insert(list, tierCpy, BLACK_A_IDX);
        }
        for (i = 0; i <= 2; ++i) {
            if (p.redPRow[i]) {
                list = add_pbwd_insert(list, tierCpy, BLACK_A_IDX, RED_P_IDX, i);
            }
        }
    }

    /* Bishops. */
    if (p.missing[RED_B_IDX]) {
        if (p.blackHasRCN || p.blackPRow[0] || p.blackPRow[2] || p.blackPRow[4]) {
            list = add_insert(list, tierCpy, RED_B_IDX);
        }
        for (i = 0; i <= 4; i += 2) {
            if (p.blackPRow[i]) {
                list = add_pbwd_insert(list, tierCpy, RED_B_IDX, BLACK_P_IDX, i);
            }
        }
    }
    if (p.missing[BLACK_B_IDX]) {
        if (p.redHasRCN || p.redPRow[0] || p.redPRow[2] || p.redPRow[4]) {
            list = add_insert(list, tierCpy, BLACK_B_IDX);
        }
        for (i = 0; i <= 4; i += 2) {
            if (p.redPRow[i]) {
                list = add_pbwd_insert(list, tierCpy, BLACK_B_IDX, RED_P_IDX, i);
            }
        }
    }

    /* Pawns. */
    if (p.missing[RED_P_IDX]) {
        /* Row 0/1/2. */
        for (i = 0; i < 3; ++i) {
            list = add_pawn_insert(list, tierCpy, RED_P_IDX, i);
        }
        /* Row 3. */
        if (p.blackHasRCN) {
            list = add_pawn_insert(list, tierCpy, RED_P_IDX, 3);
        }
        /* Row 4. */
        if (p.blackHasRCNB) {
            list = add_pawn_insert(list, tierCpy, RED_P_IDX, 4);
        }
        if (p.blackPRow[5]) {
            list = add_pawn_pbwd_insert(list, tierCpy, RED_P_IDX, 4, BLACK_P_IDX, 5);
        }
        /* Row 5/6. */
        for (i = 5; i <= 6; ++i) {
            if (p.blackHasRCN || p.blackPRow[9-i]) {
                list = add_pawn_insert(list, tierCpy, RED_P_IDX, i);
            }
            if (p.blackPRow[9-i]) {
                list = add_pawn_pbwd_insert(list, tierCpy, RED_P_IDX, i, BLACK_P_IDX, 9-i);
            }
        }
    }
    if (p.missing[BLACK_P_IDX]) {
        /* Row 0/1/2. */
        for (i = 0; i < 3; ++i) {
            list = add_pawn_insert(list, tierCpy, BLACK_P_IDX, i);
        }
        /* Row 3. */
        if (p.redHasRCN) {
            list = add_pawn_insert(list, tierCpy, BLACK_P_IDX, 3);
        }
        /* Row 4. */
        if (p.redHasRCNB) {
            list = add_pawn_insert(list, tierCpy, BLACK_P_IDX, 4);
        }
        if (p.redPRow[5]) {
            list = add_pawn_pbwd_insert(list, tierCpy, BLACK_P_IDX, 4, RED_P_IDX, 5);
        }
        /* Row 5/6. */
        for (i = 5; i <= 6; ++i) {
            if (p.redHasRCN || p.redPRow[9-i]) {
                list = add_pawn_insert(list, tierCpy, BLACK_P_IDX, i);
            }
            if (p.redPRow[9-i]) {
                list = add_pawn_pbwd_insert(list, tierCpy, BLACK_P_IDX, i, RED_P_IDX, 9-i);
            }
        }
    }

    /* Knights, cannons, and rooks. */
    for (i = RED_N_IDX; i <= BLACK_R_IDX; ++i) {
        if (p.missing[i]) {
            list = add_insert(list, tierCpy, i);
            for (j = 0; j <= 5; ++j) {
                if (!(i & 1) && p.blackPRow[j]) {
                    list = add_pbwd_insert(list, tierCpy, i, BLACK_P_IDX, j);
                } else if ((i & 1) && p.redPRow[j]) {
                    list = add_pbwd_insert(list, tierCpy, i, RED_P_IDX, j);
                }
            }
        }
    }

    /* 2. PARENT TIERS BY A BACKWARD PAWN MOVE W/O REVERSE CAPTURING. */
    get_pawn_rbegin_rend(tier, RED_P_IDX, &rbegin, &rend);
    for (i = rbegin; i > rend && tier[i] < '6'; --i) {
        while (tier[i] == tier[i-1]) --i;
        ++tierCpy[i];
        if (tier_is_legal_tier(tierCpy)) list = tier_list_insert_head(list, tierCpy, change);
        --tierCpy[i];
    }

    get_pawn_rbegin_rend(tier, BLACK_P_IDX, &rbegin, &rend);
    /* Check forward black pawn moves. */
    for (i = rbegin; i > rend && tier[i] < '6'; --i) {
        while (tier[i] == tier[i-1]) --i;
        ++tierCpy[i];
        if (tier_is_legal_tier(tierCpy)) list = tier_list_insert_head(list, tierCpy, change);
        --tierCpy[i];
    }
    return list;
}

bool tier_list_contains(const TierList *list, const char *tier) {
    for (const struct TierListElem *walker = list; walker; walker = walker->next) {
        if (!strncmp(walker->tier, tier, TIER_STR_LENGTH_MAX)) return true;
    }
    return false;
}

void tier_list_destroy(TierList *list) {
    struct TierListElem *next;
    while (list) {
        next = list->next;
        free(list);
        list = next;
    }
}

/**
 * @brief Returns a dynamic array of child tiers of TIER.
 * The array should be freed by the caller of this function
 * using the tier_array_destroy function.
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
    array.changes = (tier_change_t*)safe_malloc(array.size * sizeof(tier_change_t));
    for (i = 0; i < array.size; ++i) {
        array.tiers[i] = safe_malloc(TIER_STR_LENGTH_MAX * sizeof(char));
    }

    /* Copy tier strings and changes. */
    for (walker = list, i = 0; walker; walker = walker->next, ++i) {
        memcpy(array.tiers[i], walker->tier, TIER_STR_LENGTH_MAX);
        array.changes[i] = walker->change;
    }
    tier_list_destroy(list);
    return array;
}

void tier_array_destroy(struct TierArray *array) {
    if (!array || !array->tiers) return;
    for (uint8_t i = 0; i < array->size; ++i) {
        free(array->tiers[i]);
    }
    free(array->tiers); array->tiers = NULL;
    free(array->changes); array->changes = NULL;
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
 * @brief Returns the number of unique canonical child tiers of TIER.
 */
uint8_t tier_num_canonical_child_tiers(const char *tier) {
    TierList *list = tier_get_child_tier_list(tier);
    uint8_t n = 0;
    TierList *walker = list;
    TierList *canonicalChildren = NULL;
    while (walker) {
        struct TierListElem *canonical = tier_get_canonical_tier(walker->tier);
        if (tier_list_contains(canonicalChildren, canonical->tier)) {
            /* It is possible that TIER has two children that are symmetrical
               to each other. In this case, we should only increment the child
               counter once. */
            free(canonical);
        } else {
            canonical->next = canonicalChildren;
            canonicalChildren = canonical;
            ++n;
        }
        walker = walker->next;
    }
    tier_list_destroy(canonicalChildren);
    tier_list_destroy(list);
    return n;
}

/**
 * @brief Returns the numbers of rearrangements of pieces at
 * each step of a tier size calculation as an array. Returns NULL
 * if malloc fails to allocate. The caller of this function is
 * responsible for deallocating the malloced array.
 * The calculation of a tier size is divided into 15 steps.
 * Step 0: red king and advisors.
 * Step 1: black king and advisors.
 * Step 2: red bishops.
 * Step 3: black bishops.
 * Step 4-13: pawns on each row (black's side to red) of the board.
 * Step 14: all remaining pieces.
 * @param tier: Tier string.
 */
uint64_t *tier_size_steps(const char *tier) {
    uint64_t *steps = (uint64_t*)malloc(NUM_TIER_SIZE_STEPS * sizeof(uint64_t));
    if (!steps) return NULL;
    int redPawnBegin, redPawnEnd;
    int blackPawnBegin, blackPawnEnd;
    int redPawnRow, blackPawnRow, step, i;
    get_pawn_begin_end(tier, RED_P_IDX, &redPawnBegin, &redPawnEnd);
    get_pawn_begin_end(tier, BLACK_P_IDX, &blackPawnBegin, &blackPawnEnd);

    /* King and advisors. */
    for (step = 0; step < 2; ++step) {
        switch (tier[RED_A_IDX + step]) {
        case '0':
            /* If there are no advisors, there are 9 slots
                   for the king. */
            steps[step] = 9ULL;
            break;

        case '1':
            /* King takes one of the 5 advisor slots: 5*nCr(5-1, 1);
                   King is in one of the other 4 slots: 4*nCr(5, 1). */
            steps[step] = 40ULL;
            break;

        case '2':
            /* King takes one of the 5 advisor slots: 5*nCr(5-1, 2);
                   King is in one of the other 4 slots: 4*nCr(5, 2). */
            steps[step] = 70ULL;
            break;

        default:
            printf("tier.c::tier_size_step: illegal tier\n");
            exit(1);
        }
    }

    /* Bishops. */
    for (; step < 4; ++step) {
        /* There are 7 possible slots that a bishop can be in. */
        steps[step] = choose[7][tier[RED_B_IDX + step - 2] - '0'];
    }

    // TODO: cache all inner for loops into an array storing the number of
    // red/black pawns on each row.

    /* Define row number to be 0 thru 9 where 0 is the bottom line of
       black side and 9 is the bottom line of red side. */
    for (; step < 7; ++step) {
        /* Bottom three rows of black's half-board. No black pawns should be found.
           There are nCr(9, red) ways to place red pawns on the specified row. */
        redPawnRow = 0;
        for (i = redPawnBegin; i < redPawnEnd; ++i) {
            redPawnRow += (tier[i] - '0' == step - 4);
        }
        steps[step] = choose[9][redPawnRow];
    }

    for (; step < 11; ++step) {
        redPawnRow = blackPawnRow = 0;
        for (i = redPawnBegin; i < redPawnEnd; ++i) {
            redPawnRow += (tier[i] - '0' == step - 4);
        }
        for (i = blackPawnBegin; i < blackPawnEnd; ++i) {
            blackPawnRow += (9 - (tier[i] - '0') == step - 4);
        }
        if (step < 9) {
            /* Top two rows of black's half-board. Any black pawn in these two rows
               can only appear in one of the 5 special columns. There are
               nCr(5, black)*nCr(9-black, red) ways to place all pawns on the
               specified row. */
            steps[step] = choose[5][blackPawnRow] * choose[9 - blackPawnRow][redPawnRow];
        } else {
            /* Top two rows of red's half-board. Similar to the case above.
               nCr(5, red)*nCr(9-red, black). */
            steps[step] = choose[5][redPawnRow] * choose[9 - redPawnRow][blackPawnRow];
        }
    }

    for (; step < 14; ++step) {
        /* Bottom three rows of red's half-board. No red pawns should be found.
           There are nCr(9, black) ways to place black pawns on the specified row. */
        blackPawnRow = 0;
        for (i = blackPawnBegin; i < blackPawnEnd; ++i) {
            blackPawnRow += (9 - (tier[i] - '0') == step - 4);
        }
        steps[step] = choose[9][blackPawnRow];
    }

    /* Kights, cannons, and rooks can reach any slot. The number of ways
       to place k such pieces is nCr(90-existing_pieces, k). */
    int8_t existing = 2;
    for (i = 0; i < RED_N_IDX; ++i) {
        existing += tier[i] - '0';
    }
    steps[14] = 1ULL;
    for (i = RED_N_IDX; i <= BLACK_R_IDX; ++i) {
        steps[14] = safe_mult_uint64(steps[14], choose[90 - existing][tier[i] - '0']);
        existing += tier[i] - '0';
    }
    return steps;
}

uint64_t tier_size(const char *tier) {
    uint64_t size = 2ULL; // Red or black's turn.
    uint64_t *steps = tier_size_steps(tier);
    if (!steps) {
        printf("tier_size: tier_size_steps OOM\n");
        exit(1);
    }
    for (int i = 0; i < NUM_TIER_SIZE_STEPS; ++i) {
        size = safe_mult_uint64(size, steps[i]);
    }
    free(steps);
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

/* Assumes pawnsPerRow has at least 20 Bytes of space, first 10 spaces
   are reserved for red pawns per row, and the second 10 spaces are
   reserved for black pawns. */
void tier_get_pawns_per_row(const char *tier, uint8_t *pawnsPerRow) {
    int redPBegin, redPEnd, blackPBegin, blackPEnd, i;
    get_pawn_begin_end(tier, RED_P_IDX, &redPBegin, &redPEnd);
    get_pawn_begin_end(tier, BLACK_P_IDX, &blackPBegin, &blackPEnd);
    memset(pawnsPerRow, 0, 20 * sizeof(uint8_t));
    for (i = redPBegin; i < redPEnd; ++i) {
        ++pawnsPerRow[tier[i] - '0'];
    }
    for (i = blackPBegin; i < blackPEnd; ++i) {
        ++pawnsPerRow[19 - tier[i] + '0']; // 10 + (9 - (tier[i] - '0'))
    }
}
/**************************** End Tier Utilities ****************************/

/***************************** Helper Functions ******************************/

static uint64_t safe_add_uint64(uint64_t lhs, uint64_t rhs) {
    if (!lhs || !rhs || lhs > UINT64_MAX - rhs) {
        return 0ULL;
    }
    return lhs + rhs;
}

static uint64_t safe_mult_uint64(uint64_t lhs, uint64_t rhs) {
    if (!lhs || !rhs || lhs > UINT64_MAX / rhs) {
        return 0ULL;
    }
    return lhs * rhs;
}

static void str_shift_left(char *str, int8_t idx) {
    while (str[idx]) {
        str[idx] = str[idx + 1];
        ++idx;
    }
}

static void str_insert(char *str, char c, int8_t idx) {
    char tmp;
    while (c) {
        tmp = str[idx];
        str[idx] = c;
        c = tmp;
        ++idx;
    }
    str[idx] = '\0';
}

static void get_pawn_begin_end(const char *tier, int8_t pawnIdx, int *begin, int *end) {
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

static void get_pawn_rbegin_rend(const char *tier, int8_t pawnIdx, int *rbegin, int *rend) {
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
    if (!row) printf("move_pawn_forward: tier [%s] is trying to move a row-0 pawn forward\n", tier);
    assert(row); // cannot move a row-0 pawn forward.
    int rbegin, rend, i;
    get_pawn_rbegin_rend(tier, pawnIdx, &rbegin, &rend);
    for (i = rbegin; i > rend && tier[i] != '0'+row; --i);
    assert(tier[i] == '0'+row);
    --tier[i];
}

static void move_pawn_backward(char *tier, int pawnIdx, int row) {
    assert(row < 6); // cannot move a row-6 pawn backward.
    int begin, end, i;
    get_pawn_begin_end(tier, pawnIdx, &begin, &end);
    for (i = begin; i < end && tier[i] != '0'+row; ++i);
    ++tier[i];
}

static TierList *rm_insert(TierList *list, char *tier, int8_t idx) {
    tier_change_t change;
    change.captureIdx = idx;
    change.pawnIdx = INVALID_IDX;
    change.captureRow = change.pawnRow = -1; // Unused.

    --tier[idx];
    list = tier_list_insert_head(list, tier, change);
    ++tier[idx];
    return list;
}

static TierList *rm_pawn_insert(TierList *list, char *tier, int8_t idx, int8_t row) {
    tier_change_t change;
    change.captureIdx = idx;
    change.captureRow = row;
    change.pawnIdx = INVALID_IDX;
    change.pawnRow = -1; // Unused.

    rm_pawn(tier, idx, row);
    list = tier_list_insert_head(list, tier, change);
    add_pawn(tier, idx, row);
    return list;
}

static TierList *rm_pfwd_insert(TierList *list, char *tier, int8_t pieceIdx,
                                int8_t pawnIdx, int8_t pawnRow) {
    tier_change_t change;
    change.captureIdx = pieceIdx;
    change.captureRow = -1; // Unused.
    change.pawnIdx = pawnIdx;
    change.pawnRow = pawnRow - 1;

    --tier[pieceIdx];
    move_pawn_forward(tier, pawnIdx, pawnRow);
    /* Moving a pawn forward may result in an illegal tier. */
    if (tier_is_legal_tier(tier)) list = tier_list_insert_head(list, tier, change);
    move_pawn_backward(tier, pawnIdx, pawnRow-1);
    ++tier[pieceIdx];
    return list;
}

static TierList *rm_pawn_pfwd_insert(TierList *list, char *tier, int8_t captureIdx,
                                     int8_t captureRow, int8_t fwdIdx, int8_t fwdRow) {
    tier_change_t change;
    change.captureIdx = captureIdx;
    change.captureRow = captureRow;
    change.pawnIdx = fwdIdx;
    change.pawnRow = fwdRow - 1;

    move_pawn_forward(tier, fwdIdx, fwdRow);
    rm_pawn(tier, captureIdx, captureRow);
    /* A tier is guaranteed to be legel if at least one pawn has been captured. */
    list = tier_list_insert_head(list, tier, change);
    add_pawn(tier, captureIdx, captureRow);
    move_pawn_backward(tier, fwdIdx, fwdRow-1);
    return list;
}

static TierList *add_insert(TierList *list, char *tier, int8_t idx) {
    tier_change_t change;
    change.captureIdx = idx;
    change.pawnIdx = INVALID_IDX;
    change.captureRow = change.pawnRow = -1; // Unused.

    ++tier[idx];
    list = tier_list_insert_head(list, tier, change);
    --tier[idx];
    return list;
}

static TierList *add_pawn_insert(TierList *list, char *tier, int8_t idx, int8_t row) {
    tier_change_t change;
    change.captureIdx = idx;
    change.captureRow = row;
    change.pawnIdx = INVALID_IDX;
    change.pawnRow = -1; // Unused.

    add_pawn(tier, idx, row);
    /* Adding a pawn may result in an illegal tier. */
    if (tier_is_legal_tier(tier)) list = tier_list_insert_head(list, tier, change);
    rm_pawn(tier, idx, row);
    return list;
}

static TierList *add_pbwd_insert(TierList *list, char *tier, int8_t pieceIdx,
                                 int8_t pawnIdx, int8_t pawnRow) {
    tier_change_t change;
    change.captureIdx = pieceIdx;
    change.captureRow = -1; // Unused.
    change.pawnIdx = pawnIdx;
    change.pawnRow = pawnRow + 1;

    ++tier[pieceIdx];
    move_pawn_backward(tier, pawnIdx, pawnRow);
    /* Moving a pawn backward may result in an illegal tier. */
    if (tier_is_legal_tier(tier)) list = tier_list_insert_head(list, tier, change);
    move_pawn_forward(tier, pawnIdx, pawnRow+1);
    --tier[pieceIdx];
    return list;
}

static TierList *add_pawn_pbwd_insert(TierList *list, char *tier, int8_t addIdx,
                                      int8_t addRow, int8_t bwdIdx, int8_t bwdRow) {
    tier_change_t change;
    change.captureIdx = addIdx;
    change.captureRow = addRow;
    change.pawnIdx = bwdIdx;
    change.pawnRow = bwdRow + 1;

    move_pawn_backward(tier, bwdIdx, bwdRow);
    add_pawn(tier, addIdx, addRow);
    /* Guaranteed to be legal since the newly added pawn is not on the same row. */
    list = tier_list_insert_head(list, tier, change);
    rm_pawn(tier, addIdx, addRow);
    move_pawn_forward(tier, bwdIdx, bwdRow+1);
    return list;
}

/*************************** END Helper Functions ****************************/
