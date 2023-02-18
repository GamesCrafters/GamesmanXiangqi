#ifndef GAME_H
#define GAME_H
#include <stdbool.h>
#include <stdint.h>
#include "tier.h"

#define ILLEGAL_NUM_CHILD_POS UINT8_MAX
#define BOARD_EMPTY_CELL '-'
#define BOARD_RED_KING 'K'
#define BOARD_RED_ADVISOR 'A'
#define BOARD_RED_BISHOP 'B'
#define BOARD_RED_PAWN 'P'
#define BOARD_RED_KNIGHT 'N'
#define BOARD_RED_CANNON 'C'
#define BOARD_RED_ROOK 'R'
#define BOARD_BLACK_KING 'k'
#define BOARD_BLACK_ADVISOR 'a'
#define BOARD_BLACK_BISHOP 'b'
#define BOARD_BLACK_PAWN 'p'
#define BOARD_BLACK_KNIGHT 'n'
#define BOARD_BLACK_CANNON 'c'
#define BOARD_BLACK_ROOK 'r'

typedef struct Piece {
    char token;
    int8_t row;
    int8_t col;
} piece_t;

typedef struct Board {
    char layout[90];
    piece_t redPieces[17];
    piece_t blackPieces[17];
    bool redTurn;
} board_t;

uint8_t game_num_child_pos(const char *tier, uint64_t hash, board_t *board);
uint64_t *game_get_parents(const char *tier, uint64_t hash, tier_change_t change, board_t *board);

#endif // GAME_H
