#ifndef GAMECONSTANTS_H
#define GAMECONSTANTS_H
#include <stdbool.h>
#include "common.h"

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

#define NUM_MOVES_MAX 128
#define ILLEGAL_NUM_MOVES UINT8_MAX
#define ILLEGAL_HASH UINT64_MAX

typedef struct Scope {
    int8_t rowMin;
    int8_t colMin;
    int8_t rowMax;
    int8_t colMax;
} scope_t;

extern const int8_t pieceIdxLookup[INVALID_IDX + 3];
extern const scope_t scopes[INVALID_IDX + 3];
extern const bool validSlotLookup[INVALID_IDX + 3][10][9];

#endif // GAMECONSTANTS_H
