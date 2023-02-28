#ifndef GAME_H
#define GAME_H
#include <stdbool.h>
#include <stdint.h>
#include "tier.h"

#define ILLEGAL_NUM_CHILD_POS UINT8_MAX
#define ILLEGAL_NUM_CHILD_POS_OOM (UINT8_MAX - 1)
#define ILLEGAL_POSITION_ARRAY_SIZE UINT8_MAX

#define BOARD_ROWS 10
#define BOARD_COLS 9
#define BOARD_EMPTY_CELL INVALID_IDX
#define BOARD_RED_KING RED_K_IDX
#define BOARD_RED_ADVISOR RED_A_IDX
#define BOARD_RED_BISHOP RED_B_IDX
#define BOARD_RED_PAWN RED_P_IDX
#define BOARD_RED_KNIGHT RED_N_IDX
#define BOARD_RED_CANNON RED_C_IDX
#define BOARD_RED_ROOK RED_R_IDX
#define BOARD_BLACK_KING BLACK_K_IDX
#define BOARD_BLACK_ADVISOR BLACK_A_IDX
#define BOARD_BLACK_BISHOP BLACK_B_IDX
#define BOARD_BLACK_PAWN BLACK_P_IDX
#define BOARD_BLACK_KNIGHT BLACK_N_IDX
#define BOARD_BLACK_CANNON BLACK_C_IDX
#define BOARD_BLACK_ROOK BLACK_R_IDX

typedef struct Piece {
    int8_t token;
    int8_t row;
    int8_t col;
} piece_t;

typedef struct Board {
    int8_t layout[90];
    piece_t redPieces[17];
    piece_t blackPieces[17];
    bool blackTurn;
    bool valid;
} board_t;

typedef struct PositionArray {
    uint64_t *array;
    uint8_t size;
} pos_array_t;

uint8_t game_num_child_pos(const char *tier, uint64_t hash, board_t *board);
pos_array_t game_get_parents(const char *tier, uint64_t hash, const char *parentTier,
                             tier_change_t change, board_t *board);

uint64_t hash(const char *tier, const board_t *board);
bool unhash(board_t *board, const char *tier, uint64_t hash);
void clear_board(board_t *board);

void print_board(board_t *board);

#endif // GAME_H
