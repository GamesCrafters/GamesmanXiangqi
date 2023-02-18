#include "game.h"
#include "misc.h"
#include <string.h>
#include "types.h"
#include <stdio.h>

#define BOARD_COLS 9

static bool is_legal_pos(const char *tier, uint64_t hash);
static void unhash(board_t *board, const char *tier, uint64_t hash);
static uint64_t hash(const board_t *board);
static uint8_t num_moves(const char *layout, int8_t row, int8_t col);
static void clear_board(board_t *board);
static void add_parents(uint64_t *parents, uint8_t *parentsSize, board_t *board,
                        int8_t row, int8_t col);
static void add_parents_rev(uint64_t *parents, uint8_t *parentsSize, board_t *board,
                            tier_change_t change, int8_t row, int8_t col);
static void add_parents_pbwd(uint64_t *parents, uint8_t *parentsSize, board_t *board,
                             tier_change_t change, int8_t row, int8_t col);
static void add_parents_rev_pbwd(uint64_t *parents, uint8_t *parentsSize, board_t *board,
                                 tier_change_t change, int8_t row, int8_t col);

/**
 * @brief Returns the number of legal child positions of HASH in TIER, or
 * UINT8_MAX if the given HASH is illegal in TIER. BOARD is guaranteed to
 * be reset to empty state after call to this function.
 * @param tier: tier of the parent position.
 * @param hash: hash of the parent position inside TIER.
 * @param board: this global board should be pre-allocated and empty
 * initialized by the caller.
 */
uint8_t game_num_child_pos(const char *tier, uint64_t hash, board_t *board) {
    uint8_t count = 0;
    piece_t *pieces;
    if (!is_legal_pos(tier, hash)) return ILLEGAL_NUM_CHILD_POS;

    unhash(board, tier, hash);
    pieces = board->redTurn ? board->redPieces : board->blackPieces;
    for (int i = 0; pieces[i].token != '\0'; ++i) {
        count += num_moves(board->layout, pieces[i].row, pieces[i].col);
    }
    clear_board(board);
    return count;
}

// Assumes tier+hash is legal.
uint64_t *game_get_parents(const char *tier, uint64_t hash, tier_change_t change, board_t *board) {
    uint64_t *parents = (uint64_t*)safe_malloc(256*sizeof(uint64_t));
    uint8_t parentsSize = 0;
    piece_t *pieces = board->redTurn ? board->blackPieces : board->redPieces;
    unhash(board, tier, hash);
    for (int i = 0; pieces[i].token != '\0'; ++i) {
        if (change.captureIdx == INVALID_IDX && change.pawnIdx == INVALID_IDX) {
            /* No reverse capture, no backward pawn move. */
            add_parents(parents, &parentsSize, board, pieces[i].row, pieces[i].col);
        } else if (change.captureIdx == INVALID_IDX) {
            /* Backward pawn move only. */

        } else if (change.pawnIdx == INVALID_IDX) {
            /* Reverse capture only. */
        } else {
            /* Move pawn backward to reverse capture. */
        }
    }
    parents[parentsSize] = UINT64_MAX;
    return parents;
}

static bool is_legal_pos(const char *tier, uint64_t hash) {
    // TODO
    (void)tier;
    (void)hash;
    return false;
}

// Assumes board->layout is pre-allocated and contains all dashes ('-').
static void unhash(board_t *board, const char *tier, uint64_t hash) {
    // TODO
    (void)board;
    (void)tier;
    (void)hash;
}

static uint64_t hash(const board_t *board) {
    // TODO
    (void)board;
    return 0;
}

static uint8_t num_moves(const char *layout, int8_t row, int8_t col) {
    // TODO
    (void)layout;
    (void)row;
    (void)col;
    return 0;
}

static void clear_board(board_t *board) {
    int i;
    piece_t *pieces = board->redPieces;
    for (i = 0; pieces[i].token != '\0'; ++i) {
        board->layout[pieces[i].row*BOARD_COLS + pieces[i].col] = '-';
    }
    pieces[0].token = '\0';

    pieces = board->blackPieces;
    for (i = 0; pieces[i].token != '\0'; ++i) {
        board->layout[pieces[i].row*BOARD_COLS + pieces[i].col] = '-';
    }
    pieces[0].token = '\0';
}

typedef struct Scope {
    int8_t rowMin;
    int8_t colMin;
    int8_t rowMax;
    int8_t colMax;
} scope_t;

static inline bool in_scope(scope_t scope, int8_t row, int8_t col) {
    return row >= scope.rowMin && row <= scope.rowMax &&
            col >= scope.colMin && col <= scope.colMax;
}

static inline bool is_empty(board_t *board, int8_t row, int8_t col) {
    return board->layout[(row)*BOARD_COLS + col] == BOARD_EMPTY_CELL;
}

// src is the piece to undoMove, dest is the empty space that it undoMoves to.
static void move_piece_append(uint64_t *parents,
                              uint8_t *parentsSize,
                              board_t *board,
                              int8_t destRow,
                              int8_t destCol,
                              int8_t srcRow,
                              int8_t srcCol,
                              char replace) {
    int8_t destIdx = destRow*BOARD_COLS + destCol;
    int8_t srcIdx = srcRow*BOARD_COLS + srcCol;

    board->layout[destIdx] = board->layout[srcIdx];
    board->layout[srcIdx] = replace;
    // TODO: if hash needs correct red and black pieces list, need to update them here.
    parents[(*parentsSize)++] = hash(board);
    board->layout[srcIdx] = board->layout[destIdx];
    board->layout[destIdx] = BOARD_EMPTY_CELL;
}

// Assuming no reverse capturing or backward pawn move.
static void add_parents(uint64_t *parents,
                        uint8_t *parentsSize,
                        board_t *board,
                        int8_t row,
                        int8_t col) {
    scope_t scope;
    int8_t i, j;
    char piece = board->layout[row*BOARD_COLS + col];

    /* Preset scope to whole board. */
    scope.rowMin = 0; scope.colMin = 0;
    scope.rowMax = 9; scope.colMax = 8;

    switch (piece) {
    case BOARD_RED_KING: case BOARD_BLACK_KING:
        scope.rowMin =     7*(piece == BOARD_RED_KING); scope.colMin = 3;
        scope.rowMax = 2 + 7*(piece == BOARD_RED_KING); scope.colMax = 5;
        for (i = 0; i <= 1; ++i) {
            j = 1 - i;
            if (in_scope(scope, row+i, col+j) && is_empty(board, row+i, col+j)) {
                move_piece_append(parents, parentsSize, board, row+i, col+j, row, col, BOARD_EMPTY_CELL);
            }
            if (in_scope(scope, row-i, col-j) && is_empty(board, row-i, col-j)) {
                move_piece_append(parents, parentsSize, board, row-i, col-j, row, col, BOARD_EMPTY_CELL);
            }
        }
        break;

    case BOARD_RED_ADVISOR: case BOARD_BLACK_ADVISOR:
        scope.rowMin =     7*(piece == BOARD_RED_ADVISOR); scope.colMin = 3;
        scope.rowMax = 2 + 7*(piece == BOARD_RED_ADVISOR); scope.colMax = 5;
        for (i = -1; i <= 1; i += 2) for (j = -1; j <= 1; j += 2) {
            if (in_scope(scope, row+i, col+j) && is_empty(board, row+i, col+j)) {
                move_piece_append(parents, parentsSize, board, row+i, col+j, row, col, BOARD_EMPTY_CELL);
            }
        }
        break;

    case BOARD_RED_BISHOP: case BOARD_BLACK_BISHOP:
        scope.rowMin =     5*(piece == BOARD_RED_BISHOP);
        scope.rowMax = 4 + 5*(piece == BOARD_RED_BISHOP);
        for (i = -2; i <= 2; i += 4) for (j = -2; j <= 2; j += 4) {
            /* Also need to check if the blocking point is empty. */
            if (in_scope(scope, row+i, col+j) && is_empty(board, row+i, col+j) &&
                    is_empty(board, row + i/2, col + j/2)) {
                move_piece_append(parents, parentsSize, board, row+i, col+j, row, col, BOARD_EMPTY_CELL);
            }
        }
        break;

    case BOARD_RED_PAWN: case BOARD_BLACK_PAWN:
        scope.rowMin =     5*(piece == BOARD_BLACK_PAWN);
        scope.rowMax = 4 + 5*(piece == BOARD_BLACK_PAWN);
        for (j = -1; j <= 1; j += 2) {
            if (in_scope(scope, row, col+j) && is_empty(board, row, col+j)) {
                move_piece_append(parents, parentsSize, board, row, col+j, row, col, BOARD_EMPTY_CELL);
            }
        }
        break;

    case BOARD_RED_KNIGHT: case BOARD_BLACK_KNIGHT:
        for (i = -1; i <= 1; i += 2) for (j = -1; j <= 1; j += 2) {
            /* If the blocking point (row+i, col+j) is empty. */
            if (in_scope(scope, row+i, col+j) && is_empty(board, row+i, col+j)) {
                if (in_scope(scope, row + i*2, col+j) && is_empty(board, row + i*2, col+j)) {
                    move_piece_append(parents, parentsSize, board, row + i*2, col+j, row, col, BOARD_EMPTY_CELL);
                }
                if (in_scope(scope, row+i, col + j*2) && is_empty(board, row+i, col + j*2)) {
                    move_piece_append(parents, parentsSize, board, row+i, col + j*2, row, col, BOARD_EMPTY_CELL);
                }
            }
        }
        break;

    case BOARD_RED_CANNON: case BOARD_BLACK_CANNON: case BOARD_RED_ROOK: case BOARD_BLACK_ROOK:
        // up
        for (i = -1; in_scope(scope, row+i, col) && is_empty(board, row+i, col); --i) {
            move_piece_append(parents, parentsSize, board, row+i, col, row, col, BOARD_EMPTY_CELL);
        }
        // down
        for (i = 1; in_scope(scope, row+i, col) && is_empty(board, row+i, col); ++i) {
            move_piece_append(parents, parentsSize, board, row+i, col, row, col, BOARD_EMPTY_CELL);
        }
        // left
        for (j = -1; in_scope(scope, row, col+j) && is_empty(board, row, col+j); --j) {
            move_piece_append(parents, parentsSize, board, row, col+j, row, col, BOARD_EMPTY_CELL);
        }
        // right
        for (j = 1; in_scope(scope, row, col+j) && is_empty(board, row, col+j); ++j) {
            move_piece_append(parents, parentsSize, board, row, col+j, row, col, BOARD_EMPTY_CELL);
        }
        break;

    default:
        printf("game.c::add_parents: invalid piece on board.layout\n");
        exit(1);
    }
}

























