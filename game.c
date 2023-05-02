#include "common.h"
#include "game.h"
#include "misc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_MOVES_MAX 128
#define ILLEGAL_NUM_MOVES UINT8_MAX
#define ILLEGAL_HASH UINT64_MAX

extern unsigned long long choose[CHOOSE_ROWS][CHOOSE_COLS];

typedef struct Scope {
    int8_t rowMin;
    int8_t colMin;
    int8_t rowMax;
    int8_t colMax;
} scope_t;

/* +2 to pieceIdx to get index in this array.   K,k,A,a,B,b,P,p,N,n,C,c,R,r,- */
const int8_t pieceIdxLookup[INVALID_IDX + 3] = {1,1,2,2,1,1,1,1,1,2,3,4,5,6,0};

//**************************** CONVERT THESE TO MACROS ******************************
static inline int8_t layout_at(const int8_t *layout, int8_t row, int8_t col) {
    return layout[row*BOARD_COLS + col];
}

static inline bool in_scope(scope_t scope, int8_t row, int8_t col) {
    return row >= scope.rowMin && row <= scope.rowMax &&
            col >= scope.colMin && col <= scope.colMax;
}

static inline bool in_board(int8_t row, int8_t col) {
    return row >= 0 && row < BOARD_ROWS &&
            col >= 0 && col < BOARD_COLS;
}

static inline bool is_empty(const int8_t *layout, int8_t row, int8_t col) {
    return layout_at(layout, row, col) == BOARD_EMPTY_CELL;
}

static inline bool is_red(int8_t pieceIdx) {
    return !(pieceIdx & 1) && pieceIdx != INVALID_IDX;
}

static inline bool is_black(int8_t pieceIdx) {
    return (pieceIdx & 1) && pieceIdx != INVALID_IDX;
}

static inline bool can_capture(board_t *board, int8_t row, int8_t col) {
    return is_empty(board->layout, row, col) || (board->blackTurn ^ is_black(layout_at(board->layout, row, col)));
}

static inline bool is_opponent_king(board_t *board, int8_t row, int8_t col) {
    return (board->blackTurn && layout_at(board->layout, row, col) == BOARD_RED_KING) ||
            (!board->blackTurn && layout_at(board->layout, row, col) == BOARD_BLACK_KING);
}
//***********************************************************************************

/********************* Helper Function Declarations *********************/
static uint64_t *hash_to_steps(const char *tier, uint64_t hash);
static uint64_t steps_to_hash(const char *tier, const uint64_t *steps);
static bool steps_to_board(board_t *board, const char *tier, uint64_t *steps);
static uint64_t *board_to_steps(const char *tier, const board_t *board);

static bool is_legal_pos(board_t *board);
static bool is_valid_slot(int8_t pieceIdx, int8_t row, int8_t col);

static uint8_t num_moves(board_t *board, int8_t idx, bool testOnly);
static bool add_children(ext_pos_array_t *children, board_t *board, int8_t idx);
static scope_t get_scope(int8_t piece);
static void move_piece(board_t *board, int8_t destRow, int8_t destCol,
                       int8_t srcRow, int8_t srcCol, int8_t replace);
static void move_piece_append(ext_pos_array_t *children, board_t *board,
                              int8_t destRow, int8_t destCol,
                              int8_t srcRow, int8_t srcCol);
static void undomove_piece_append(pos_array_t *parents,
                                  const char *tier, board_t *board,
                                  int8_t destRow, int8_t destCol,
                                  int8_t srcRow, int8_t srcCol,
                                  int8_t replace);
static void add_parents(pos_array_t *parents, const char *tier,
                        board_t *board, int8_t row, int8_t col, int8_t revIdx);
static bool flying_general_possible(const board_t *board);
static uint64_t combiCount(const uint8_t *counts, uint8_t numPieces);
static uint64_t hash_cruncher(const int8_t *layout, const uint8_t *slots, uint8_t size,
                              int8_t pieceMin, int8_t pieceMax,
                              uint8_t *rems, uint8_t numPieces);
static void hash_uncruncher(uint64_t hash, board_t *board, uint8_t *piecesSizes,
                            uint8_t *slots, uint8_t numSlots,
                            const int8_t *tokens, uint8_t *rems, uint8_t numTokens);

static void board_to_sa_position(sa_position_t *pos, board_t *board);
/******************* End Helper Function Declarations *******************/

/**************************** Game Utilities ****************************/

/**
 * @brief Returns the number of legal child positions of HASH in TIER.
 * Returns ILLEGAL_NUM_CHILD_POS if the given HASH is illegal in TIER.
 * Returns ILLEGAL_NUM_CHILD_POS_OOM if heap memory runs out during
 * this function call. BOARD is guaranteed to be reset to empty state
 * after call to this function.
 * @param tier: tier of the parent position.
 * @param hash: hash of the parent position inside TIER.
 * @param board: this global board should be pre-allocated and empty
 * initialized by the caller.
 */
uint8_t game_num_child_pos(const char *tier, uint64_t hash, board_t *board) {
    uint8_t count = 0, nmoves;
    if (!game_unhash(board, tier, hash)) return ILLEGAL_NUM_CHILD_POS_OOM;
    if (!board->valid || flying_general_possible(board)) {
        clear_board(board);
        return ILLEGAL_NUM_CHILD_POS;
    }
    for (int8_t i = board->blackTurn*BOARD_PIECES_OFFSET;
            board->pieces[i].token != BOARD_EMPTY_CELL; ++i) {
        nmoves = num_moves(board, i, false);
        if (nmoves == ILLEGAL_NUM_MOVES) {
            clear_board(board);
            return ILLEGAL_NUM_CHILD_POS;
        }
        count += nmoves;
    }
    clear_board(board);
    return count;
}

ext_pos_array_t game_get_children(const char *tier, uint64_t hash) {
    ext_pos_array_t children;
    board_t board;

    memset(&children, 0, sizeof(children));
    game_init_board(&board);
    if (!game_unhash(&board, tier, hash)) exit(1);
    if (!board.valid || flying_general_possible(&board)) {
        children.size = ILLEGAL_POSITION_ARRAY_SIZE;
        return children;
    }

    children.array = (sa_position_t*)safe_malloc(NUM_MOVES_MAX * sizeof(sa_position_t));
    for (int8_t i = board.blackTurn*BOARD_PIECES_OFFSET;
            board.pieces[i].token != BOARD_EMPTY_CELL; ++i) {
        if (!add_children(&children, &board, i)) {
            free(children.array); children.array = NULL;
            children.size = ILLEGAL_POSITION_ARRAY_SIZE;
            return children;
        }
    }
    return children;
}

/**
 * @brief Returns a PositionArray that contains all the parent
 * positions of position HASH in tier TIER that satisfies the
 * TierChange required by CHANGE. A TierChange object specifies
 * which type of piece is captured and/or the row number of the
 * pawn that moved forward and is generated with child tiers.
 * By specifying the tier change, we restrict the tier in which
 * the parent position can be in. This function assumes that
 * the given TIER and HASH are both valid.
 * @param tier: tier of the child position.
 * @param hash: hash of the child position.
 * @param change: tier change from parent tier (the tier we
 * wish to return to) to child tier (the current TIER.)
 * @param board: this global board should be pre-allocated and empty
 * initialized by the caller.
 * @return A PositionArray which contains a pointer to an array of
 * parent position hashes and the size of that array. If OOM,
 * the size of the array is set to ILLEGAL_POSITION_ARRAY_SIZE_OOM
 * and the array pointer is set to NULL. Otherwise, the array is
 * malloced and should be freed by the caller of this function.
 */
pos_array_t game_get_parents(const char *tier, uint64_t hash, const char *parentTier,
                             tier_change_t change, board_t *board) {
    pos_array_t parents;
    memset(&parents, 0, sizeof(parents));
    if (!game_unhash(board, tier, hash)) {
        parents.size = ILLEGAL_POSITION_ARRAY_SIZE_OOM;
        goto bail_out;
    }

    /* Return empty parents array if turn does not match tier change. */
    if ((!board->blackTurn && (is_black(change.captureIdx) || is_red(change.pawnIdx))) ||
            (board->blackTurn && (is_red(change.captureIdx) || is_black(change.pawnIdx)))) {
        goto bail_out;
    }

    bool pbwd = (change.pawnIdx != INVALID_IDX);
    bool revBlackP = (change.captureIdx == BLACK_P_IDX);
    bool revRedP = (change.captureIdx == RED_P_IDX);
    bool revp = revRedP || revBlackP;
    bool revOK;
    int8_t row, col, token, destRow;
    parents.array = (uint64_t*)malloc(NUM_MOVES_MAX * sizeof(uint64_t));
    if (!parents.array) {
        parents.size = ILLEGAL_POSITION_ARRAY_SIZE_OOM;
        goto bail_out;
    }

    /* Convert row number for black pawns. */
    if (change.captureIdx == BLACK_P_IDX) change.captureRow = 9 - change.captureRow;
    if (change.pawnIdx == BLACK_P_IDX) change.pawnRow = 9 - change.pawnRow;

    for (int8_t i = (!board->blackTurn)*BOARD_PIECES_OFFSET;
            board->pieces[i].token != BOARD_EMPTY_CELL; ++i) {
        token = board->pieces[i].token;
        row = board->pieces[i].row;
        col = board->pieces[i].col;
        destRow = row - 1 + ((token == BOARD_RED_PAWN) << 1); // row+1 if red, row-1 if black.
        revOK = !revp || (row == change.captureRow);
        if (!pbwd && is_valid_slot(change.captureIdx, row, col) && revOK) {
            /* No backward pawn move:
               1. If no reverse capture, is_valid_slot always return true
                  and we can always add parents;
               2. If reverse capturing non-pawn pieces, add parents if src
                  slot is valid for the piece put back;
               3. If reverse capturing pawns, add parents if slot and row
                  number are both valid. */
            add_parents(&parents, parentTier, board, row, col, change.captureIdx);
        } else if (pbwd && (token == change.pawnIdx) && (row == change.pawnRow) &&
                   is_valid_slot(token, destRow, col) && is_empty(board->layout, destRow, col) &&
                   is_valid_slot(change.captureIdx, row, col) && revOK) {
            /* Move pawn backward: always need to check if token is the pawn to move and
               the destination is a valid position where the pawn can reach. Then check
               the same conditions as above. */
            undomove_piece_append(&parents, parentTier, board, destRow, col,
                              row, col, change.captureIdx);
        }
    }

    /* Check for illegal positions due to OOM in the array. */
    for (uint8_t i = 0; i < parents.size; ++i) {
        if (parents.array[i] == ILLEGAL_HASH) {
            free(parents.array); parents.array = NULL;
            parents.size = ILLEGAL_POSITION_ARRAY_SIZE_OOM;
            break;
        }
    }

bail_out:
    clear_board(board);
    return parents;
}

inline bool game_is_black_turn(uint64_t hash) {
    return (hash & 1);
}

/**
 * @brief Returns the hash of BOARD in TIER. Returns ILLEGAL_HASH if fails
 * to allocate space for intermediate steps.
 */
uint64_t game_hash(const char *tier, const board_t *board) {
    uint64_t *steps = board_to_steps(tier, board);
    uint64_t res = steps_to_hash(tier, steps);
    free(steps);
    return res;
}

// Assumes board->layout is pre-allocated and contains all BOARD_EMPTY_CELL.
/**
 * @brief Unhashes TIER and HASH to BOARD, which is assumed to be empty
 * and valid. If HASH is invalid for TIER, BOARD->valid is set to false.
 * Returns true no OOM error occurs, false otherwise.
 * @note A HASH is invalid if some pieces are overlapping.
 */
bool game_unhash(board_t *board, const char *tier, uint64_t hash) {
    uint64_t *steps = hash_to_steps(tier, hash);
    bool success = steps_to_board(board, tier, steps);
    free(steps);
    return success;
}

static void take_pieces_off_and_rotate(piece_t *pieces, int8_t *layout) {
    for (int8_t i = 0; pieces[i].token != BOARD_EMPTY_CELL; ++i) {
        layout[pieces[i].row*BOARD_COLS + pieces[i].col] = BOARD_EMPTY_CELL;
        pieces[i].token ^= 1;
        pieces[i].row = BOARD_ROWS - 1 - pieces[i].row;
        pieces[i].col = BOARD_COLS - 1 - pieces[i].col;
    }
}

static void place_pieces(piece_t *pieces, int8_t *layout) {
    for (int8_t i = 0; pieces[i].token != BOARD_EMPTY_CELL; ++i) {
        layout[pieces[i].row*BOARD_COLS + pieces[i].col] = pieces[i].token;
    }
}

uint64_t game_get_noncanonical_hash(const char *canonicalTier, uint64_t canonicalHash,
                                    const char *noncanonicalTier, board_t *board) {
    game_unhash(board, canonicalTier, canonicalHash);

    /* Take all pieces off the board, swap the color, and rotate by 180 degrees. */
    take_pieces_off_and_rotate(board->pieces, board->layout);
    take_pieces_off_and_rotate(board->pieces + BOARD_PIECES_OFFSET, board->layout);
    piece_t tmp[16];
    memcpy(tmp, board->pieces, 16*sizeof(piece_t));
    memcpy(board->pieces, board->pieces + BOARD_PIECES_OFFSET, 16*sizeof(piece_t));
    memcpy(board->pieces + BOARD_PIECES_OFFSET, tmp, 16*sizeof(piece_t));

    /* Place the new set of pieces on the board. */
    place_pieces(board->pieces, board->layout);
    place_pieces(board->pieces + BOARD_PIECES_OFFSET, board->layout);
    board->blackTurn = !board->blackTurn;

    uint64_t res = game_hash(noncanonicalTier, board);
    clear_board(board);
    return res;
}

void game_init_board(board_t *board) {
    memset(board->layout, BOARD_EMPTY_CELL, BOARD_SIZE);
    for (uint8_t i = 0; i < 2*MAX_PIECES_EACH_SIDE + 2; ++i) {
        board->pieces[i].token = BOARD_EMPTY_CELL;
    }
}

static void clear_board_helper(piece_t *pieces, int8_t *layout) {
    for (int8_t i = 0; pieces[i].token != BOARD_EMPTY_CELL; ++i) {
        layout[pieces[i].row*BOARD_COLS + pieces[i].col] = BOARD_EMPTY_CELL;
    }
    pieces[0].token = BOARD_EMPTY_CELL;
}

void clear_board(board_t *board) {
    clear_board_helper(board->pieces, board->layout);
    clear_board_helper(board->pieces + BOARD_PIECES_OFFSET, board->layout);
}

/************************** End Game Utilities **************************/

/********************** Helper Function Definitions *********************/

/**
 * @brief Returns true if the position as represented by BOARD
 * is a legal one, false otherwise.
 */
static bool is_legal_pos(board_t *board) {
    if (flying_general_possible(board)) return false;
    uint8_t nmoves;
    for (int8_t i = board->blackTurn*BOARD_PIECES_OFFSET;
            board->pieces[i].token != BOARD_EMPTY_CELL; ++i) {
        nmoves = num_moves(board, i, true);
        if (nmoves == ILLEGAL_NUM_MOVES) return false;
    }
    return true;
}

static uint64_t *hash_to_steps(const char *tier, uint64_t hash) {
    uint64_t *steps = (uint64_t*)malloc((NUM_TIER_SIZE_STEPS + 1)*sizeof(uint64_t));
    if (!steps) return NULL; // OOM.
    uint64_t *stepsMax = tier_size_steps(tier);
    if (!stepsMax) {
        free(steps);
        return NULL;
    }
    /* Turn bit */
    steps[NUM_TIER_SIZE_STEPS] = hash & 1ULL;
    hash >>= 1;
    /* Steps */
    for (int i = NUM_TIER_SIZE_STEPS - 1; i >= 0; --i) {
        steps[i] = hash % stepsMax[i];
        hash /= stepsMax[i];
    }
    free(stepsMax);
    return steps;
}

static uint64_t steps_to_hash(const char *tier, const uint64_t *steps) {
    uint64_t res = 0ULL;
    uint64_t *stepsMax = tier_size_steps(tier);
    if (!steps || !stepsMax) return ILLEGAL_HASH; // OOM.
    /* Steps */
    for (int i = 0; i < NUM_TIER_SIZE_STEPS; ++i) {
        res *= stepsMax[i];
        res += steps[i];
    }
    /* Turn bit */
    res = (res << 1) | steps[NUM_TIER_SIZE_STEPS];
    free(stepsMax);
    return res;
}

static uint8_t kingSlot[3][3] = {
    {0, 0, 0},
    {1, 0, 2},
    {0, 3, 0}
};

static uint8_t kingIdx[4] = {67, 75, 77, 85};

/**
 * @brief Set SLOTS according to LAYOUT, STEP, and SUBSTEP, and returns the
 * number of slots set.
 */
static uint8_t set_slots(uint8_t *slots, const int8_t *layout, int step, uint8_t substep) {
    int parity = step & 1;
    uint8_t i, j;
    switch (step) {
    case 0: case 1:
        slots[0] = 66 - 63*step; slots[1] = 68 - 63*step; slots[2] = 76 - 63*step;
        slots[3] = 84 - 63*step; slots[4] = 86 - 63*step;
        return 5;

    case 2: case 3:
        slots[0] = 47 - 45*parity; slots[1] = 51 - 45*parity; slots[2] = 63 - 45*parity;
        slots[3] = 67 - 45*parity; slots[4] = 71 - 45*parity; slots[5] = 83 - 45*parity;
        slots[6] = 87 - 45*parity;
        return 7;

    case 4: case 5: case 6: case 11: case 12: case 13:
        for (i = 0; i < BOARD_COLS; ++i) {
            slots[i] = i + BOARD_COLS*(step - 4); // skip the first (step - 4) rows.
        }
        return BOARD_COLS;

    case 7: case 8: case 9: case 10:
        if (!substep) {
            for (j = 0; j < BOARD_COLS; j += 2) { // columns 0, 2, 4, 6, 8.
                slots[j >> 1] = j + BOARD_COLS*(step - 4); // skip the first (step - 4) rows.
            }
            return 5;
        } else {
            for (i = 0, j = 0; j < BOARD_COLS; ++j) {
                /* The following line assumes BOARD_RED_PAWN + 1 == BOARD_BLACK_PAWN. */
                if (layout_at(layout, step - 4, j) != BOARD_RED_PAWN + (step < 9)) {
                    slots[i++] = j + BOARD_COLS*(step - 4);
                }
            }
            return i;
        }

    case 14:
        for (i = 0, j = 0; j < BOARD_SIZE; ++j) {
            if (layout[j] >= BOARD_RED_KNIGHT) {
                slots[i++] = j;
            }
        }
        return i;

    default:
        return UINT8_MAX;
    }
}

static bool steps_to_board(board_t *board, const char *tier, uint64_t *steps) {
    if (!steps) return false; // OOM in previous step.
    int step, parity;
    uint8_t i, j, nLessRestrictedP, nMoreRestrictedP;
    uint8_t slots[BOARD_SIZE];
    int8_t piecesToPlace[7];
    uint8_t rems[7];
    uint8_t piecesSizes[2] = {0, 0};
    uint8_t pawnsPerRow[2 * BOARD_ROWS];

    board->valid = true; // Should an error occur, set this value to false in that step.
    tier_get_pawns_per_row(tier, pawnsPerRow);
    piecesToPlace[0] = BOARD_EMPTY_CELL; // Empty cell is always the 0-th piece to place.

    /* STEP 0 & 1: KINGS AND ADVISORS. */
    for (step = 0; step < 2; ++step) {
        piece_t *pieces = board->pieces + step * BOARD_PIECES_OFFSET;
        set_slots(slots, NULL, step, 0);
        piecesToPlace[1] = BOARD_RED_KING + step;
        piecesToPlace[2] = BOARD_RED_ADVISOR + step;

        switch (tier[RED_A_IDX + step]) {
        case '0':
            /* No advisors. */
            pieces[0].token = BOARD_RED_KING + step;
            pieces[0].row = steps[step] / 3 + 7*(1 - step);
            pieces[0].col = steps[step] % 3 + 3;
            board->layout[pieces[0].row*BOARD_COLS + pieces[0].col] = pieces[0].token;
            ++piecesSizes[step];
            break;

        case '1':
            if (steps[step] < 20ULL) {
                /* King does not occupy advisor slots, 20 possible configurations. */
                /* First place the king. */
                pieces[0].token = BOARD_RED_KING + step;
                i = kingIdx[steps[step] / 5] - 63*step;
                pieces[0].row = i / BOARD_COLS;
                pieces[0].col = i % BOARD_COLS;
                board->layout[i] = pieces[0].token;
                ++piecesSizes[step];

                /* Then place the advisor. */
                rems[0] = 4; rems[1] = 0; rems[2] = 1;
                hash_uncruncher(steps[step] % 5ULL, board, piecesSizes,
                                slots, 5, piecesToPlace, rems, 3);
            } else {
                /* King occupies one of the advisor slots, 20 possible configurations. */
                rems[0] = 3; rems[1] = 1; rems[2] = 1;
                hash_uncruncher(steps[step] - 20ULL, board, piecesSizes,
                                slots, 5, piecesToPlace, rems, 3);
                i = 0;
                while (pieces[i].token != piecesToPlace[1]) ++i;
                piece_t tmp = pieces[0];
                pieces[0] = pieces[i];
                pieces[i] = tmp;
            }
            break;

        case '2':
            if (steps[step] < 40ULL) {
                /* King does not occupy advisor slots, 40 possible configurations. */
                pieces[0].token = BOARD_RED_KING + step;
                i = kingIdx[steps[step] / 10] - 63*step;
                pieces[0].row = i / BOARD_COLS;
                pieces[0].col = i % BOARD_COLS;
                board->layout[i] = pieces[0].token;
                ++piecesSizes[step];

                /* Then place the advisor. */
                rems[0] = 3; rems[1] = 0; rems[2] = 2;
                hash_uncruncher(steps[step] % 10ULL, board, piecesSizes,
                                slots, 5, piecesToPlace, rems, 3);
            } else {
                /* King occupies one of the advisor slots, 30 possible configurations. */
                rems[0] = 2; rems[1] = 1; rems[2] = 2;
                hash_uncruncher(steps[step] - 40ULL, board, piecesSizes,
                                slots, 5, piecesToPlace, rems, 3);
                i = 0;
                while (pieces[i].token != piecesToPlace[1]) ++i;
                piece_t tmp = pieces[0];
                pieces[0] = pieces[i];
                pieces[i] = tmp;
            }
            break;
        }
    }

    /* STEP 2 & 3: BISHOPS. */
    for (; step < 4; ++step) {
        parity = step & 1;
        set_slots(slots, NULL, step, 0);
        rems[1] = tier[RED_B_IDX + parity] - '0';
        rems[0] = 7 - rems[1];
        piecesToPlace[1] = BOARD_RED_BISHOP + parity;
        hash_uncruncher(steps[step], board, piecesSizes,
                        slots, 7, piecesToPlace, rems, 2);
    }

    /* STEPS 4 - 6: RED PAWNS IN THE TOP THREE ROWS. */
    for (; step < 7; ++step) {
        set_slots(slots, NULL, step, 0);
        rems[1] = pawnsPerRow[step - 4]; // # red pawns in curr row.
        rems[0] = BOARD_COLS - rems[1];  // # empty slots in curr row.
        piecesToPlace[1] = BOARD_RED_PAWN;
        hash_uncruncher(steps[step], board, piecesSizes,
                        slots, BOARD_COLS, piecesToPlace, rems, 2);
    }

    /* STEPS 7 - 10: PAWNS IN ROW 3 THRU ROW 6. */
    for (; step < 11; ++step) {
        nMoreRestrictedP = pawnsPerRow[BOARD_ROWS * (step < 9) + step - 4];
        nLessRestrictedP = pawnsPerRow[BOARD_ROWS * (step >= 9) + step - 4];

        /* Unhash the more restricted pawns first. */
        set_slots(slots, NULL, step, 0); // substep 0.
        rems[1] = nMoreRestrictedP; // # "more restricted" pawns in curr row.
        rems[0] = 5 - rems[1];      // # empty slots at the 5 locations above.
        piecesToPlace[1] = BOARD_RED_PAWN + (step < 9);
        hash_uncruncher(steps[step] / choose[BOARD_COLS - nMoreRestrictedP][nLessRestrictedP],
                board, piecesSizes, slots, 5, piecesToPlace, rems, 2);

        /* Then unhash the less restricted pawns. */
        i = set_slots(slots, board->layout, step, 1); // substep 1.
        rems[1] = nLessRestrictedP; // # "less restricted" pawns in curr row.
        rems[0] = i - rems[1];      // # remaining empty slots in curr row.
        piecesToPlace[1] = BOARD_RED_PAWN + (step >= 9);
        hash_uncruncher(steps[step] % choose[BOARD_COLS - nMoreRestrictedP][nLessRestrictedP],
                board, piecesSizes, slots, i, piecesToPlace, rems, 2);
    }

    /* STEPS 11 - 13: BLACK PAWNS IN THE BOTTOM THREE ROWS. */
    for (; step < 14; ++step) {
        set_slots(slots, NULL, step, 0);
        rems[1] = pawnsPerRow[BOARD_ROWS + step - 4]; // # black pawns in curr row.
        rems[0] = BOARD_COLS - rems[1];               // # empty slots in curr row.
        piecesToPlace[1] = BOARD_BLACK_PAWN;
        hash_uncruncher(steps[step], board, piecesSizes,
                        slots, BOARD_COLS, piecesToPlace, rems, 2);
    }

    /* STEP 14: KNIGHTS, CANNONS, AND ROOKS. */
    i = set_slots(slots, board->layout, step, 0);
    rems[0] = i;
    for (j = RED_N_IDX; j <= BLACK_R_IDX; ++j) {
        rems[j - RED_N_IDX + 1] = tier[j] - '0';
        rems[0] -= tier[j] - '0';
        piecesToPlace[j - RED_N_IDX + 1] = BOARD_RED_KNIGHT + j - RED_N_IDX;
    }
    hash_uncruncher(steps[step], board, piecesSizes, slots, i, piecesToPlace, rems, 7);

    /* STEP 15: TURN BIT. */
    board->blackTurn = steps[15];

    /* NULL-terminate the pieces arrays. */
    board->pieces[piecesSizes[0]] = (piece_t){BOARD_EMPTY_CELL, 0, 0};
    board->pieces[BOARD_PIECES_OFFSET + piecesSizes[1]] = (piece_t){BOARD_EMPTY_CELL, 0, 0};
    return true;
}

static uint64_t *board_to_steps(const char *tier, const board_t *board) {
    uint64_t *steps = (uint64_t*)malloc((NUM_TIER_SIZE_STEPS + 1)*sizeof(uint64_t));
    if (!steps) return NULL; // OOM.

    int step;
    uint8_t i, j;
    uint8_t slots[BOARD_SIZE];
    uint8_t rems[7];
    uint8_t pawnsPerRow[2 * BOARD_ROWS];

    tier_get_pawns_per_row(tier, pawnsPerRow);

    /* STEPS 0 & 1: KINGS AND ADVISORS. */
    for (step = 0; step < 2; ++step) {
        set_slots(slots, NULL, step, 0);
        i = board->pieces[step * BOARD_PIECES_OFFSET].row - 7*(1 - step);
        j = board->pieces[step * BOARD_PIECES_OFFSET].col - 3;

        switch (tier[RED_A_IDX + step]) {
        case '0':
            /* No advisors. */
            steps[step] = 3ULL*i + j;
            break;

        case '1':
            if ((i + j) & 1) {
                /* King does not occupy advisor slots, 20 possible configurations. */
                rems[0] = 4; rems[1] = 0; rems[2] = 1;
                steps[step] = 5ULL * kingSlot[i][j] +
                        hash_cruncher(board->layout, slots, 5, BOARD_RED_KING, BOARD_BLACK_ADVISOR, rems, 3);
            } else {
                /* King occupies one of the advisor slots, 20 possible configurations. */
                rems[0] = 3; rems[1] = 1; rems[2] = 1;
                steps[step] = 20ULL +
                        hash_cruncher(board->layout, slots, 5, BOARD_RED_KING, BOARD_BLACK_ADVISOR, rems, 3);
            }
            break;

        case '2':
            if ((i + j) & 1) {
                /* King does not occupy advisor slots, 40 possible configurations. */
                rems[0] = 3; rems[1] = 0; rems[2] = 2;
                steps[step] = 10ULL * kingSlot[i][j] +
                        hash_cruncher(board->layout, slots, 5, BOARD_RED_KING, BOARD_BLACK_ADVISOR, rems, 3);
            } else {
                /* King occupies one of the advisor slots, 30 possible configurations. */
                rems[0] = 2; rems[1] = 1; rems[2] = 2;
                steps[step] = 40ULL +
                        hash_cruncher(board->layout, slots, 5, BOARD_RED_KING, BOARD_BLACK_ADVISOR, rems, 3);
            }
            break;
        }
    }

    /* STEPS 2 & 3: BISHOPS. */
    for (; step < 4; ++step) {
        set_slots(slots, NULL, step, 0);
        rems[1] = tier[RED_B_IDX + (step & 1)] - '0';
        rems[0] = 7 - rems[1];
        steps[step] = hash_cruncher(board->layout, slots, 7, BOARD_RED_BISHOP, BOARD_BLACK_BISHOP, rems, 2);
    }

    /* STEPS 4 - 6: RED PAWNS IN THE TOP THREE ROWS. */
    for (; step < 7; ++step) {
        set_slots(slots, NULL, step, 0);
        rems[1] = pawnsPerRow[step - 4]; // # red pawns in curr row.
        rems[0] = BOARD_COLS - rems[1];  // # empty slots in curr row.
        steps[step] = hash_cruncher(board->layout, slots, BOARD_COLS, BOARD_RED_PAWN, BOARD_RED_PAWN, rems, 2);
    }

    /* STEPS 7 - 10: PAWNS IN ROW 3 THRU ROW 6. */
    for (; step < 11; ++step) {
        /* Hash the more restricted pawns first. */
        set_slots(slots, NULL, step, 0); // substep 0.
        rems[1] = pawnsPerRow[BOARD_ROWS * (step < 9) + step - 4]; // # "more restricted" pawns in curr row.
        rems[0] = 5 - rems[1];                                     // # empty slots at the 5 locations above.
        steps[step] = hash_cruncher(board->layout, slots, 5, BOARD_RED_PAWN + (step < 9), BOARD_RED_PAWN + (step < 9), rems, 2);

        /* Then hash the less restricted pawns. */
        i = set_slots(slots, board->layout, step, 1); // substep 1.
        rems[1] = pawnsPerRow[BOARD_ROWS * (step >= 9) + step - 4]; // # "less restricted" pawns in curr row.
        rems[0] = i - rems[1];           // # remaining empty slots in curr row.
        steps[step] *= choose[i][rems[1]]; // Must calculate this first as hash_cruncher modifies rems.
        steps[step] += hash_cruncher(board->layout, slots, i, BOARD_RED_PAWN + (step >= 9), BOARD_RED_PAWN + (step >= 9), rems, 2);
    }

    /* STEPS 11 - 13: BLACK PAWNS IN THE BOTTOM THREE ROWS. */
    for (; step < 14; ++step) {
        set_slots(slots, NULL, step, 0);
        rems[1] = pawnsPerRow[BOARD_ROWS + step - 4]; // # black pawns in curr row.
        rems[0] = BOARD_COLS - rems[1];               // # empty slots in curr row.
        steps[step] = hash_cruncher(board->layout, slots, BOARD_COLS, BOARD_BLACK_PAWN, BOARD_BLACK_PAWN, rems, 2);
    }

    /* STEP 14: KNIGHTS, CANNONS, AND ROOKS. */
    i = set_slots(slots, board->layout, step, 0);
    rems[0] = i;
    for (j = RED_N_IDX; j <= BLACK_R_IDX; ++j) {
        rems[j - RED_N_IDX + 1] = tier[j] - '0';
        rems[0] -= tier[j] - '0';
    }
    steps[14] = hash_cruncher(board->layout, slots, i, BOARD_RED_KNIGHT, BOARD_BLACK_ROOK, rems, 7);

    /* STEP 15: TURN BIT. */
    steps[NUM_TIER_SIZE_STEPS] = board->blackTurn;
    return steps;
}

static bool is_valid_move(board_t *board, int8_t idx, int8_t i, int8_t j) {
    int8_t row = board->pieces[idx].row;
    int8_t col = board->pieces[idx].col;
    const int8_t piece = layout_at(board->layout, row, col);
    scope_t scope = get_scope(piece);
    bool res = true;

    /* "Row-6 pawns" can move forward into a cell that is not in the above scope. */
    bool fwdException = (piece == BOARD_RED_PAWN && row == 6 && i == -1 && j == 0) ||
            (piece == BOARD_BLACK_PAWN && row == 3 && i == 1 && j == 0);

    /* Move is immediately invalid if attempting to move a piece
       off borders or to capture a friendly piece. */
    res &= (in_scope(scope, row+i, col+j) || fwdException) &&
            can_capture(board, row+i, col+j);

    /* Special rule for knights and bishops: cannot be blocked. */
    res &= (piece != BOARD_RED_BISHOP && piece != BOARD_BLACK_BISHOP &&
            piece != BOARD_RED_KNIGHT && piece != BOARD_BLACK_KNIGHT) ||
            is_empty(board->layout, row + i/2, col + j/2);

    if (!res) return false;

    /* Make move and see if the resulting position is valid. */
    int8_t hold = layout_at(board->layout, row+i, col+j);
    move_piece(board, row+i, col+j, row, col, BOARD_EMPTY_CELL);
    res = is_legal_pos(board);
    move_piece(board, row, col, row+i, col+j, hold);
    return res;
}

/**
 * @brief Returns the number of legal moves of the piece at (ROW, COL)
 * in LAYOUT. Returns ILLEGAL_NUM_MOVES if the given piece can capture
 * the opponent king directly.
 */
static uint8_t num_moves(board_t *board, int8_t idx, bool testOnly) {
    uint8_t nmoves = 0;
    int8_t row = board->pieces[idx].row;
    int8_t col = board->pieces[idx].col;
    int8_t i, j, encounter;
    const int8_t piece = layout_at(board->layout, row, col);

    switch (piece) {
    case BOARD_RED_KING: case BOARD_BLACK_KING:
        /* A king can never capture the opponent's king. */
        if (testOnly) return 0;
        for (i = 0; i <= 1; ++i) {
            j = 1 - i;
            nmoves += is_valid_move(board, idx, i, j);
            nmoves += is_valid_move(board, idx, -i, -j);
        }
        break;

    case BOARD_RED_ADVISOR: case BOARD_BLACK_ADVISOR:
        /* An advisor can never capture the opponent's king. */
        if (testOnly) return 0;
        for (i = -1; i <= 1; i += 2) for (j = -1; j <= 1; j += 2) {
            nmoves += is_valid_move(board, idx, i, j);
        }
        break;

    case BOARD_RED_BISHOP: case BOARD_BLACK_BISHOP:
        /* A bishop can never capture the opponent's king. */
        if (testOnly) return 0;
        for (i = -2; i <= 2; i += 4) for (j = -2; j <= 2; j += 4) {
            nmoves += is_valid_move(board, idx, i, j);
        }
        break;

    case BOARD_RED_PAWN: case BOARD_BLACK_PAWN:
        /* A pawn may capture the opponent's king. */

        /* Horizontal moves. */
        for (j = -1; j <= 1; j += 2) {
            if (in_board(row, col+j) && is_opponent_king(board, row, col+j)) return ILLEGAL_NUM_MOVES;
            nmoves += !testOnly && is_valid_move(board, idx, 0, j);
        }

        /* Forward move. */
        i = -1 + ((piece == BOARD_BLACK_PAWN) << 1);
        if (in_board(row + i, col) && is_opponent_king(board, row+i, col)) return ILLEGAL_NUM_MOVES;
        nmoves += !testOnly && is_valid_move(board, idx, i, 0);
        break;

    case BOARD_RED_KNIGHT: case BOARD_BLACK_KNIGHT:
        /* A knight may capture the opponent's king. */
        for (i = -1; i <= 1; i += 2) for (j = -1; j <= 1; j += 2) {
            if ((in_board(row + i*2, col+j) &&
                 is_empty(board->layout, row+i, col) &&
                 is_opponent_king(board, row + i*2, col+j)) ||
                    (in_board(row+i, col + j*2) &&
                     is_empty(board->layout, row, col+j) &&
                     is_opponent_king(board, row+i, col + j*2))
                    ) return ILLEGAL_NUM_MOVES;
            nmoves += !testOnly && is_valid_move(board, idx, i*2, j);
            nmoves += !testOnly && is_valid_move(board, idx, i, j*2);
        }
        break;

    case BOARD_RED_CANNON: case BOARD_BLACK_CANNON:
        /* A cannon may capture the opponent's king. */
        // up
        for (i = -1, encounter = 0; in_board(row+i, col) && encounter < 2; --i) {
            encounter += !is_empty(board->layout, row+i, col);
            if (encounter == 2 && is_opponent_king(board, row+i, col)) return ILLEGAL_NUM_MOVES;
            nmoves += !testOnly && !(encounter & 1) && is_valid_move(board, idx, i, 0);
        }
        // down
        for (i = 1, encounter = 0; in_board(row+i, col) && encounter < 2; ++i) {
            encounter += !is_empty(board->layout, row+i, col);
            if (encounter == 2 && is_opponent_king(board, row+i, col)) return ILLEGAL_NUM_MOVES;
            nmoves += !testOnly && !(encounter & 1) && is_valid_move(board, idx, i, 0);
        }
        // left
        for (j = -1, encounter = 0; in_board(row, col+j) && encounter < 2; --j) {
            encounter += !is_empty(board->layout, row, col+j);
            if (encounter == 2 && is_opponent_king(board, row, col+j)) return ILLEGAL_NUM_MOVES;
            nmoves += !testOnly && !(encounter & 1) && is_valid_move(board, idx, 0, j);
        }
        // right
        for (j = 1, encounter = 0; in_board(row, col+j) && encounter < 2; ++j) {
            encounter += !is_empty(board->layout, row, col+j);
            if (encounter == 2 && is_opponent_king(board, row, col+j)) return ILLEGAL_NUM_MOVES;
            nmoves += !testOnly && !(encounter & 1) && is_valid_move(board, idx, 0, j);
        }
        break;

    case BOARD_RED_ROOK: case BOARD_BLACK_ROOK:
        // up
        for (i = -1, encounter = 0; in_board(row+i, col) && encounter < 1; --i) {
            encounter += !is_empty(board->layout, row+i, col);
            if (is_opponent_king(board, row+i, col)) return ILLEGAL_NUM_MOVES;
            nmoves += !testOnly && is_valid_move(board, idx, i, 0);
        }
        // down
        for (i = 1, encounter = 0; in_board(row+i, col) && encounter < 1; ++i) {
            encounter += !is_empty(board->layout, row+i, col);
            if (is_opponent_king(board, row+i, col)) return ILLEGAL_NUM_MOVES;
            nmoves += !testOnly && is_valid_move(board, idx, i, 0);
        }
        // left
        for (j = -1, encounter = 0; in_board(row, col+j) && encounter < 1; --j) {
            encounter += !is_empty(board->layout, row, col+j);
            if (is_opponent_king(board, row, col+j)) return ILLEGAL_NUM_MOVES;
            nmoves += !testOnly && is_valid_move(board, idx, 0, j);
        }
        // right
        for (j = 1, encounter = 0; in_board(row, col+j) && encounter < 1; ++j) {
            encounter += !is_empty(board->layout, row, col+j);
            if (is_opponent_king(board, row, col+j)) return ILLEGAL_NUM_MOVES;
            nmoves += !testOnly && is_valid_move(board, idx, 0, j);
        }
        break;

    default:
        printf("game.c::num_moves: invalid piece on board.layout\n");
        exit(1);
    }
    return nmoves;
}

static bool add_children(ext_pos_array_t *children, board_t *board, int8_t idx) {
    int8_t row = board->pieces[idx].row;
    int8_t col = board->pieces[idx].col;
    int8_t i, j, encounter;
    const int8_t piece = layout_at(board->layout, row, col);

    switch (piece) {
    case BOARD_RED_KING: case BOARD_BLACK_KING:
        /* A king can never capture the opponent's king. */
        for (i = 0; i <= 1; ++i) {
            j = 1 - i;
            if (is_valid_move(board, idx, i, j)) {
                move_piece_append(children, board, row+i, col+j, row, col);
            }
            if (is_valid_move(board, idx, -i, -j)) {
                move_piece_append(children, board, row-i, col-j, row, col);
            }
        }
        break;

    case BOARD_RED_ADVISOR: case BOARD_BLACK_ADVISOR:
        /* An advisor can never capture the opponent's king. */
        for (i = -1; i <= 1; i += 2) for (j = -1; j <= 1; j += 2) {
            if (is_valid_move(board, idx, i, j)) {
                move_piece_append(children, board, row+i, col+j, row, col);
            }
        }
        break;

    case BOARD_RED_BISHOP: case BOARD_BLACK_BISHOP:
        /* A bishop can never capture the opponent's king. */
        for (i = -2; i <= 2; i += 4) for (j = -2; j <= 2; j += 4) {
            if (is_valid_move(board, idx, i, j)) {
                move_piece_append(children, board, row+i, col+j, row, col);
            }
        }
        break;

    case BOARD_RED_PAWN: case BOARD_BLACK_PAWN:
        /* A pawn may capture the opponent's king. */

        /* Horizontal moves. */
        for (j = -1; j <= 1; j += 2) {
            if (in_board(row, col+j) && is_opponent_king(board, row, col+j)) return false;
            if (is_valid_move(board, idx, 0, j)) {
                move_piece_append(children, board, row, col+j, row, col);
            }
        }

        /* Forward move. */
        i = -1 + ((piece == BOARD_BLACK_PAWN) << 1);
        if (in_board(row + i, col) && is_opponent_king(board, row+i, col)) return false;
        if (is_valid_move(board, idx, i, 0)) {
            move_piece_append(children, board, row+i, col, row, col);
        }
        break;

    case BOARD_RED_KNIGHT: case BOARD_BLACK_KNIGHT:
        /* A knight may capture the opponent's king. */
        for (i = -1; i <= 1; i += 2) for (j = -1; j <= 1; j += 2) {
            if ((in_board(row + i*2, col+j) &&
                 is_empty(board->layout, row+i, col) &&
                 is_opponent_king(board, row + i*2, col+j)) ||
                    (in_board(row+i, col + j*2) &&
                     is_empty(board->layout, row, col+j) &&
                     is_opponent_king(board, row+i, col + j*2))
                    ) return false;
            if (is_valid_move(board, idx, i*2, j)) {
                move_piece_append(children, board, row + i*2, col+j, row, col);
            }
            if (is_valid_move(board, idx, i, j*2)) {
                move_piece_append(children, board, row+i, col + j*2, row, col);
            }
        }
        break;

    case BOARD_RED_CANNON: case BOARD_BLACK_CANNON:
        /* A cannon may capture the opponent's king. */
        // up
        for (i = -1, encounter = 0; in_board(row+i, col) && encounter < 2; --i) {
            encounter += !is_empty(board->layout, row+i, col);
            if (encounter == 2 && is_opponent_king(board, row+i, col)) return false;
            if (!(encounter & 1) && is_valid_move(board, idx, i, 0)) {
                move_piece_append(children, board, row+i, col, row, col);
            }
        }
        // down
        for (i = 1, encounter = 0; in_board(row+i, col) && encounter < 2; ++i) {
            encounter += !is_empty(board->layout, row+i, col);
            if (encounter == 2 && is_opponent_king(board, row+i, col)) return false;
            if (!(encounter & 1) && is_valid_move(board, idx, i, 0)) {
                move_piece_append(children, board, row+i, col, row, col);
            }
        }
        // left
        for (j = -1, encounter = 0; in_board(row, col+j) && encounter < 2; --j) {
            encounter += !is_empty(board->layout, row, col+j);
            if (encounter == 2 && is_opponent_king(board, row, col+j)) return false;
            if (!(encounter & 1) && is_valid_move(board, idx, 0, j)) {
                move_piece_append(children, board, row, col+j, row, col);
            }
        }
        // right
        for (j = 1, encounter = 0; in_board(row, col+j) && encounter < 2; ++j) {
            encounter += !is_empty(board->layout, row, col+j);
            if (encounter == 2 && is_opponent_king(board, row, col+j)) return false;
            if (!(encounter & 1) && is_valid_move(board, idx, 0, j)) {
                move_piece_append(children, board, row, col+j, row, col);
            }
        }
        break;

    case BOARD_RED_ROOK: case BOARD_BLACK_ROOK:
        // up
        for (i = -1, encounter = 0; in_board(row+i, col) && encounter < 1; --i) {
            encounter += !is_empty(board->layout, row+i, col);
            if (is_opponent_king(board, row+i, col)) return false;
            if (is_valid_move(board, idx, i, 0)) {
                move_piece_append(children, board, row+i, col, row, col);
            }
        }
        // down
        for (i = 1, encounter = 0; in_board(row+i, col) && encounter < 1; ++i) {
            encounter += !is_empty(board->layout, row+i, col);
            if (is_opponent_king(board, row+i, col)) return false;
            if (is_valid_move(board, idx, i, 0)) {
                move_piece_append(children, board, row+i, col, row, col);
            }
        }
        // left
        for (j = -1, encounter = 0; in_board(row, col+j) && encounter < 1; --j) {
            encounter += !is_empty(board->layout, row, col+j);
            if (is_opponent_king(board, row, col+j)) return false;
            if (is_valid_move(board, idx, 0, j)) {
                move_piece_append(children, board, row, col+j, row, col);
            }
        }
        // right
        for (j = 1, encounter = 0; in_board(row, col+j) && encounter < 1; ++j) {
            encounter += !is_empty(board->layout, row, col+j);
            if (is_opponent_king(board, row, col+j)) return false;
            if (is_valid_move(board, idx, 0, j)) {
                move_piece_append(children, board, row, col+j, row, col);
            }
        }
        break;

    default:
        printf("game.c::add_children: invalid piece on board.layout\n");
        exit(1);
    }
    return true;
}

static void pieces_shift_left(piece_t *pieces, int8_t i) {
    while (pieces[i].token != BOARD_EMPTY_CELL) {
        pieces[i] = pieces[i + 1];
        ++i;
    }
}

static void pieces_insert(piece_t *pieces, int8_t token, int8_t row, int8_t col) {
    int8_t i = 0;
    while (pieces[i].token != BOARD_EMPTY_CELL) ++i;
    pieces[i].token = token;
    pieces[i].row = row;
    pieces[i].col = col;
    pieces[i + 1].token = BOARD_EMPTY_CELL;
}

/**
 * @brief Moves the piece at (SRCROW, SRCCOL) to (DESTROW, DESTCOL),
 * replacing the source piece with REPLACE, and updates BOARD.
 * Does not check if BOARD or the given move is valid.
 */
static void move_piece(board_t *board, int8_t destRow, int8_t destCol,
                       int8_t srcRow, int8_t srcCol, int8_t replace) {
    // TODO: rename these!
    int8_t destIdx = destRow*BOARD_COLS + destCol;
    int8_t srcIdx = srcRow*BOARD_COLS + srcCol;
    int8_t moving = layout_at(board->layout, srcRow, srcCol);
    int8_t capturing = layout_at(board->layout, destRow, destCol);
    piece_t *movingPieces, *capturingPieces;

    movingPieces = board->pieces + (!is_red(moving)) * BOARD_PIECES_OFFSET;
    capturingPieces = board->pieces + is_red(moving) * BOARD_PIECES_OFFSET;

    /* Move current piece. */
    int8_t i = 0;
    while (movingPieces[i].token != moving ||
           movingPieces[i].row != srcRow ||
           movingPieces[i].col != srcCol) ++i;
    movingPieces[i].row = destRow;
    movingPieces[i].col = destCol;

    /* Update opponent pieces array if needed. */
    if (capturing != BOARD_EMPTY_CELL) {
        i = 0;
        while (capturingPieces[i].token != capturing ||
               capturingPieces[i].row != destRow ||
               capturingPieces[i].col != destCol) ++i;
        pieces_shift_left(capturingPieces, i);
    } else if (replace != BOARD_EMPTY_CELL) {
        pieces_insert(capturingPieces, replace, srcRow, srcCol);
    }

    /* Update layout. */
    board->layout[destIdx] = board->layout[srcIdx];
    board->layout[srcIdx] = replace;

    /* Flip turn bit. */
    board->blackTurn = !board->blackTurn;
}

static void move_piece_append(ext_pos_array_t *children, board_t *board,
                              int8_t destRow, int8_t destCol,
                              int8_t srcRow, int8_t srcCol) {
    int8_t hold = layout_at(board->layout, destRow, destCol);
    move_piece(board, destRow, destCol, srcRow, srcCol, BOARD_EMPTY_CELL);
    board_to_sa_position(&children->array[children->size++], board);
    move_piece(board, srcRow, srcCol, destRow, destCol, hold);
}

// src is the piece to undoMove, dest is the empty space that it undoMoves to.
static void undomove_piece_append(pos_array_t *parents,
                                  const char *tier, board_t *board,
                                  int8_t destRow, int8_t destCol,
                                  int8_t srcRow, int8_t srcCol,
                                  int8_t replace) {
    move_piece(board, destRow, destCol, srcRow, srcCol, replace);
    if (is_legal_pos(board)) {
        parents->array[parents->size++] = game_hash(tier, board);
    }
    move_piece(board, srcRow, srcCol, destRow, destCol, BOARD_EMPTY_CELL);
}

/**
 * @brief Returns the region where PIECE can move freely. For a non-pawn
 * piece, its scope is the smallest rectangular region that contains
 * all the possible slots it can ever reach. For pawns, the scope is the
 * opponent's half board not including the slots where pawns can only
 * move forward.
 */
static scope_t get_scope(int8_t piece) {
    scope_t scope;
    switch (piece) {
    case BOARD_RED_KING: case BOARD_BLACK_KING:
        scope.rowMin =     7*(piece == BOARD_RED_KING); scope.colMin = 3;
        scope.rowMax = 2 + 7*(piece == BOARD_RED_KING); scope.colMax = 5;
        break;

    case BOARD_RED_ADVISOR: case BOARD_BLACK_ADVISOR:
        scope.rowMin =     7*(piece == BOARD_RED_ADVISOR); scope.colMin = 3;
        scope.rowMax = 2 + 7*(piece == BOARD_RED_ADVISOR); scope.colMax = 5;
        break;

    case BOARD_RED_BISHOP: case BOARD_BLACK_BISHOP:
        scope.rowMin =     5*(piece == BOARD_RED_BISHOP); scope.colMin = 0;
        scope.rowMax = 4 + 5*(piece == BOARD_RED_BISHOP); scope.colMax = 8;
        break;

    case BOARD_RED_PAWN: case BOARD_BLACK_PAWN:
        scope.rowMin =     5*(piece == BOARD_BLACK_PAWN); scope.colMin = 0;
        scope.rowMax = 4 + 5*(piece == BOARD_BLACK_PAWN); scope.colMax = 8;
        break;

    case BOARD_RED_KNIGHT: case BOARD_BLACK_KNIGHT:
    case BOARD_RED_CANNON: case BOARD_BLACK_CANNON:
    case BOARD_RED_ROOK: case BOARD_BLACK_ROOK:
    case BOARD_EMPTY_CELL:
        scope.rowMin = 0; scope.colMin = 0;
        scope.rowMax = 9; scope.colMax = 8;
        break;

    default:
        printf("game.c::get_scope: invalid piece\n");
        exit(1);
    }
    return scope;
}

/**
 * @brief Appends the legal parent positions of the position given by BOARD to the
 * PARENTS array by undo-moving the piece at (ROW, COL) and reverse capturing a
 * piece with REVIDX, assuming no backward pawn moves are allowed.
 * @param parents: array of parent positions.
 * @param tier: tier of the parent position.
 * @param board: represents the current (child) position.
 * @param row: row of the piece to undo-move.
 * @param col: column of the piece to undo-move.
 * @param revIdx: index of the piece to reverse capture. Set to BOARD_EMPTY_CELL
 * if do not want reverse capturing.
 */
static void add_parents(pos_array_t *parents, const char *tier,
                        board_t *board, int8_t row, int8_t col, int8_t revIdx) {
    const int8_t *layout = board->layout;
    int8_t i, j, encounter;
    int8_t piece = layout_at(layout, row, col);
    scope_t scope = get_scope(piece);

    switch (piece) {
    case BOARD_RED_KING: case BOARD_BLACK_KING:
        for (i = 0; i <= 1; ++i) {
            j = 1 - i;
            if (in_scope(scope, row+i, col+j) && is_empty(layout, row+i, col+j)) {
                undomove_piece_append(parents, tier, board, row+i, col+j, row, col, revIdx);
            }
            if (in_scope(scope, row-i, col-j) && is_empty(layout, row-i, col-j)) {
                undomove_piece_append(parents, tier, board, row-i, col-j, row, col, revIdx);
            }
        }
        break;

    case BOARD_RED_ADVISOR: case BOARD_BLACK_ADVISOR:
        for (i = -1; i <= 1; i += 2) for (j = -1; j <= 1; j += 2) {
            if (in_scope(scope, row+i, col+j) && is_empty(layout, row+i, col+j)) {
                undomove_piece_append(parents, tier, board, row+i, col+j, row, col, revIdx);
            }
        }
        break;

    case BOARD_RED_BISHOP: case BOARD_BLACK_BISHOP:
        for (i = -2; i <= 2; i += 4) for (j = -2; j <= 2; j += 4) {
            /* Also need to check if the blocking point is empty. */
            if (in_scope(scope, row+i, col+j) && is_empty(layout, row+i, col+j) &&
                    is_empty(layout, row + i/2, col + j/2)) {
                undomove_piece_append(parents, tier, board, row+i, col+j, row, col, revIdx);
            }
        }
        break;

    case BOARD_RED_PAWN: case BOARD_BLACK_PAWN:
        for (j = -1; j <= 1; j += 2) {
            if (in_scope(scope, row, col+j) && is_empty(layout, row, col+j)) {
                undomove_piece_append(parents, tier, board, row, col+j, row, col, revIdx);
            }
        }
        break;

    case BOARD_RED_KNIGHT: case BOARD_BLACK_KNIGHT:
        for (i = -1; i <= 1; i += 2) for (j = -1; j <= 1; j += 2) {
            /* If the blocking point (row+i, col+j) is empty. */
            if (in_scope(scope, row+i, col+j) && is_empty(layout, row+i, col+j)) {
                if (in_scope(scope, row + i*2, col+j) && is_empty(layout, row + i*2, col+j)) {
                    undomove_piece_append(parents, tier, board, row + i*2, col+j, row, col, revIdx);
                }
                if (in_scope(scope, row+i, col + j*2) && is_empty(layout, row+i, col + j*2)) {
                    undomove_piece_append(parents, tier, board, row+i, col + j*2, row, col, revIdx);
                }
            }
        }
        break;

    case BOARD_RED_CANNON: case BOARD_BLACK_CANNON:
        /* Reverse capturing. */
        if (revIdx != BOARD_EMPTY_CELL) {
            // up
            for (i = -1, encounter = 0; in_scope(scope, row+i, col) && encounter < 2; --i) {
                if (!is_empty(layout, row+i, col)) ++encounter;
                else if (encounter) undomove_piece_append(parents, tier, board, row+i, col, row, col, revIdx);
            }
            // down
            for (i = 1, encounter = 0; in_scope(scope, row+i, col) && encounter < 2; ++i) {
                if (!is_empty(layout, row+i, col)) ++encounter;
                else if (encounter) undomove_piece_append(parents, tier, board, row+i, col, row, col, revIdx);
            }
            // left
            for (j = -1, encounter = 0; in_scope(scope, row, col+j) && encounter < 2; --j) {
                if (!is_empty(layout, row, col+j)) ++encounter;
                else if (encounter) undomove_piece_append(parents, tier, board, row, col+j, row, col, revIdx);
            }
            // right
            for (j = 1, encounter = 0; in_scope(scope, row, col+j) && encounter < 2; ++j) {
                if (!is_empty(layout, row, col+j)) ++encounter;
                else if (encounter) undomove_piece_append(parents, tier, board, row, col+j, row, col, revIdx);
            }
            break;
        }
        /* Else, fall through. */

    case BOARD_RED_ROOK: case BOARD_BLACK_ROOK:
        // up
        for (i = -1; in_scope(scope, row+i, col) && is_empty(layout, row+i, col); --i) {
            undomove_piece_append(parents, tier, board, row+i, col, row, col, revIdx);
        }
        // down
        for (i = 1; in_scope(scope, row+i, col) && is_empty(layout, row+i, col); ++i) {
            undomove_piece_append(parents, tier, board, row+i, col, row, col, revIdx);
        }
        // left
        for (j = -1; in_scope(scope, row, col+j) && is_empty(layout, row, col+j); --j) {
            undomove_piece_append(parents, tier, board, row, col+j, row, col, revIdx);
        }
        // right
        for (j = 1; in_scope(scope, row, col+j) && is_empty(layout, row, col+j); ++j) {
            undomove_piece_append(parents, tier, board, row, col+j, row, col, revIdx);
        }
        break;

    default:
        printf("game.c::add_parents: invalid piece on board.layout\n");
        exit(1);
    }
}
// TODO: convert this to hard-coded lookup tables.
static bool is_valid_slot(int8_t pieceIdx, int8_t row, int8_t col) {
    int8_t layoutIdx = row*BOARD_COLS + col;
    scope_t scope = get_scope(pieceIdx);
    switch (pieceIdx) {
    case RED_K_IDX: case BLACK_K_IDX:
        return in_scope(scope, row, col);

    case RED_A_IDX: // 66 68 76 84 86
        return layoutIdx == 66 || layoutIdx == 68 || layoutIdx == 76 ||
                layoutIdx == 84 || layoutIdx == 86;

    case BLACK_A_IDX: // 3 5 13 21 23
        return layoutIdx == 3 || layoutIdx == 5 || layoutIdx == 13 ||
                layoutIdx == 21 || layoutIdx == 23;

    case RED_B_IDX: // 47 51 63 67 71 83 87
        return layoutIdx == 47 || layoutIdx == 51 || layoutIdx == 63 ||
                layoutIdx == 67 || layoutIdx == 71 || layoutIdx == 83 ||
                layoutIdx == 87;

    case BLACK_B_IDX: // 2 6 18 22 26 38 42
        return layoutIdx == 2 || layoutIdx == 6 || layoutIdx == 18 ||
                layoutIdx == 22 || layoutIdx == 26 || layoutIdx == 38 ||
                layoutIdx == 42;

    case RED_P_IDX: // scope 45 47 49 51 53 54 56 58 60 62
        return in_scope(scope, row, col) ||
                layoutIdx == 45 || layoutIdx == 47 || layoutIdx == 49 ||
                layoutIdx == 51 || layoutIdx == 53 || layoutIdx == 54 ||
                layoutIdx == 56 || layoutIdx == 58 || layoutIdx == 60 ||
                layoutIdx == 62;

    case BLACK_P_IDX: // scope 27 29 31 33 35 36 38 40 42 44
        return in_scope(scope, row, col) ||
                layoutIdx == 27 || layoutIdx == 29 || layoutIdx == 31 ||
                layoutIdx == 33 || layoutIdx == 35 || layoutIdx == 36 ||
                layoutIdx == 38 || layoutIdx == 40 || layoutIdx == 42 ||
                layoutIdx == 44;

    case RED_N_IDX: case BLACK_N_IDX: case RED_C_IDX: case BLACK_C_IDX:
    case RED_R_IDX: case BLACK_R_IDX: case INVALID_IDX:
        return true;

    default:
        printf("game.c::is_valid_slot: invalid piece\n");
        exit(1);
    }
}

static bool flying_general_possible(const board_t *board) {
    if (board->pieces[0].col != board->pieces[BOARD_PIECES_OFFSET].col) return false;
    for (int8_t i = board->pieces[BOARD_PIECES_OFFSET].row + 1; i < board->pieces[0].row; ++i) {
        if (layout_at(board->layout, i, board->pieces[0].col) != BOARD_EMPTY_CELL) {
            return false;
        }
    }
    return true;
}

static uint64_t combiCount(const uint8_t *counts, uint8_t numPieces) {
    uint64_t sum = 0, prod = 1;
    for (int8_t i = numPieces - 1; i > 0; --i) {
        sum += counts[i];
        prod *= choose[sum+counts[i - 1]][sum];
    }
    return prod;
}

static uint64_t hash_cruncher(const int8_t *layout, const uint8_t *slots, uint8_t size,
                              int8_t pieceMin, int8_t pieceMax,
                              uint8_t *rems, uint8_t numPieces) {
    uint64_t hash = 0;
    int8_t i, j, pieceIdx;
    int8_t pieceOnBoard, actualPiece;
    for (i = size - 1; i > 0; --i) {
        pieceOnBoard = layout[slots[i]];
        actualPiece = (pieceOnBoard < pieceMin || pieceOnBoard > pieceMax) ?
                    BOARD_EMPTY_CELL : pieceOnBoard;
        pieceIdx = pieceIdxLookup[actualPiece + 2]; // +2 to accommodate the kings.
        for (j = 0; j < pieceIdx; ++j) {
            if (rems[j]) {
                --rems[j];
                hash += combiCount(rems, numPieces);
                ++rems[j];
            }
        }
        --rems[pieceIdx];
    }
    return hash;
}

static void hash_uncruncher(uint64_t hash, board_t *board, uint8_t *piecesSizes,
                            uint8_t *slots, uint8_t numSlots,
                            const int8_t *tokens, uint8_t *rems, uint8_t numTokens) {
    uint64_t prevOffset = 0, currOffset;
    int i, j, pieceIdx, parity;
    for (i = numSlots - 1; i >= 0; --i) {
        currOffset = 0;
        pieceIdx = 0;
        for (j = 0; (currOffset <= hash) && (j < numTokens); ++j) {
            if (rems[j]) {
                prevOffset = currOffset;
                --rems[j];
                currOffset = prevOffset + combiCount(rems, numTokens);
                ++rems[j];
                pieceIdx = j;
            }
        }
        --rems[pieceIdx];
        /* Update layout and pieces array. */
        if (board->layout[slots[i]] != BOARD_EMPTY_CELL &&
                tokens[pieceIdx] != BOARD_EMPTY_CELL) {
            /* Overlapping pieces. */
            board->valid = false;
        }
        if (tokens[pieceIdx] != BOARD_EMPTY_CELL) {
            /* Insert only if piece is not an empty cell.
               piece_t format: {token, row, col}. */
            board->layout[slots[i]] = tokens[pieceIdx];
            parity = tokens[pieceIdx] & 1;
            board->pieces[parity * BOARD_PIECES_OFFSET + (piecesSizes[parity]++)] = 
                (piece_t){ tokens[pieceIdx], slots[i] / BOARD_COLS, slots[i] % BOARD_COLS };
        }
        hash -= prevOffset;
    }
}

static void board_to_sa_position(sa_position_t *pos, board_t *board) {
    int8_t i, j, k;
    uint8_t redPawnRow[7] = {0}, blackPawnRow[7] = {0};
    memset(pos->tier, '0', 12);
    /* Index starts from 1 to skip over kings. */
    for (i = 1; board->pieces[i].token != BOARD_EMPTY_CELL; ++i) {
        ++pos->tier[board->pieces[i].token];
        if (board->pieces[i].token == BOARD_RED_PAWN) {
            ++redPawnRow[board->pieces[i].row];
        }
    }
    for (i = BOARD_PIECES_OFFSET + 1; board->pieces[i].token != BOARD_EMPTY_CELL; ++i) {
        ++pos->tier[board->pieces[i].token];
        if (board->pieces[i].token == BOARD_BLACK_PAWN) {
            ++blackPawnRow[9 - board->pieces[i].row];
        }
    }
    k = 12;
    /* Append red pawns. */
    pos->tier[k++] = '_';
    for (i = 6; i >= 0; --i) for (j = 0; j < redPawnRow[i]; ++j) {
        pos->tier[k++] = '0' + i;
    }

    /* Append black pawns. */
    pos->tier[k++] = '_';
    for (i = 6; i >= 0; --i) for (j = 0; j < blackPawnRow[i]; ++j) {
        pos->tier[k++] = '0' + i;
    }
    pos->tier[k] = '\0';

    pos->hash = game_hash(pos->tier, board);
}

/******************** End Helper Function Definitions *******************/

static const char pieceMapping[INVALID_IDX + 3] = {'K','k','A','a','B','b','P','p','N','n','C','c','R','r',' '};

void print_board(board_t *board) {
    char graph[19][18] = {
        " - - - - - - - - ",
        "| | | |\\|/| | | |",
        " - - - - - - - - ",
        "| | | |/|\\| | | |",
        " - - - - - - - - ",
        "| | | | | | | | |",
        " - - - - - - - - ",
        "| | | | | | | | |",
        " - - - - - - - - ",
        "|     RIVER     |",
        " - - - - - - - - ",
        "| | | | | | | | |",
        " - - - - - - - - ",
        "| | | | | | | | |",
        " - - - - - - - - ",
        "| | | |\\|/| | | |",
        " - - - - - - - - ",
        "| | | |/|\\| | | |",
        " - - - - - - - - "
    };
    int8_t i;
    for (i = 0; i < BOARD_SIZE; ++i) {
        int8_t row = i / BOARD_COLS;
        int8_t col = i % BOARD_COLS;
        graph[row<<1][col<<1] = pieceMapping[board->layout[i] + 2];
    }
    printf("\n");
    for (i = 0; i < 19; ++i) {
        printf("%s\n", graph[i]);
    }
    printf("\n");
}
