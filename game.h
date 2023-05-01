#ifndef GAME_H
#define GAME_H
#include <stdbool.h>
#include <stdint.h>
#include "tier.h"

#define ILLEGAL_NUM_CHILD_POS           UINT8_MAX
#define ILLEGAL_NUM_CHILD_POS_OOM       (UINT8_MAX - 1)
#define ILLEGAL_POSITION_ARRAY_SIZE     UINT8_MAX
#define ILLEGAL_POSITION_ARRAY_SIZE_OOM (UINT8_MAX - 1)

#define BOARD_ROWS 10
#define BOARD_COLS 9
#define BOARD_SIZE (BOARD_ROWS*BOARD_COLS)
#define BOARD_EMPTY_CELL    INVALID_IDX
#define BOARD_RED_KING      RED_K_IDX
#define BOARD_RED_ADVISOR   RED_A_IDX
#define BOARD_RED_BISHOP    RED_B_IDX
#define BOARD_RED_PAWN      RED_P_IDX
#define BOARD_RED_KNIGHT    RED_N_IDX
#define BOARD_RED_CANNON    RED_C_IDX
#define BOARD_RED_ROOK      RED_R_IDX
#define BOARD_BLACK_KING    BLACK_K_IDX
#define BOARD_BLACK_ADVISOR BLACK_A_IDX
#define BOARD_BLACK_BISHOP  BLACK_B_IDX
#define BOARD_BLACK_PAWN    BLACK_P_IDX
#define BOARD_BLACK_KNIGHT  BLACK_N_IDX
#define BOARD_BLACK_CANNON  BLACK_C_IDX
#define BOARD_BLACK_ROOK    BLACK_R_IDX

typedef struct Piece {
    int8_t token;
    int8_t row;
    int8_t col;
} piece_t;

typedef struct Board {
    int8_t layout[90];
    piece_t redPieces[16];
    piece_t blackPieces[16];
    bool blackTurn;
    bool valid;
} board_t;

typedef struct PositionArray {
    uint64_t *array;
    uint8_t size;
} pos_array_t;

typedef struct StandalonePosition {
    uint64_t hash;
    char tier[TIER_STR_LENGTH_MAX];
} sa_position_t;

typedef struct ExtendedPositionArray {
    sa_position_t *array;
    uint8_t size;
} ext_pos_array_t;

uint8_t game_num_child_pos(const char *tier, uint64_t hash, board_t *board);
ext_pos_array_t game_get_children(const char *tier, uint64_t hash);
pos_array_t game_get_parents(const char *tier, uint64_t hash, const char *parentTier,
                             tier_change_t change, board_t *board);

bool game_is_black_turn(uint64_t hash);

uint64_t game_hash(const char *tier, const board_t *board);
bool game_unhash(board_t *board, const char *tier, uint64_t hash);
uint64_t game_get_noncanonical_hash(const char *canonicalTier, uint64_t canonicalHash,
                                    const char *noncanonicalTier, board_t *board);

void game_init_board(board_t *board);
void clear_board(board_t *board);
void print_board(board_t *board);

#endif // GAME_H
