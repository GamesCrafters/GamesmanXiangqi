#include "game.h"
#include "game_test.h"
#include "tiertree.h"
#include "common.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void test_hash_def(const char *tier) {
    uint64_t tierSize = tier_size(tier);
    uint64_t i, j;
    board_t board;
    memset(board.layout, BOARD_EMPTY_CELL, BOARD_ROWS * BOARD_COLS);
    board.valid = true;

    for (i = 0; i < tierSize; ++i) {
        unhash(&board, tier, i);
        if (board.valid) {
            j = hash(tier, &board);
            if (j != i) {
                printf("game_test.c::test_hash_def: hash(unhash(%"PRIu64")) in tier %s"
                       " evaluates to %"PRIu64", which is not equal to the"
                       " original hash value.\n", i, tier, j);
                getc(stdin);
            }
        } else {
            printf("invalid position %"PRIu64" in tier %s\n", i, tier);
        }
        clear_board(&board);
    }
}

void game_test_sanity(void) {
    tier_scan_driver(0, test_hash_def);
    printf("game_test.c::game_test_sanity passed.\n");
}
