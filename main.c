//#include "tests/game_test.h"
#include "solver.h"
#include "common.h"
#include "game.h"
#include <string.h>
#include <stdio.h>

// TODO: replace for loops with memcpy.
// TODO: replace vector implementation mallocs with reallocs.
int main() {
    make_triangle();
//    board_t board;
//    memset(board.layout, BOARD_EMPTY_CELL, 90);
//    board.valid = true;

//    for (int i = 0; i < 108; ++i) {
//        uint8_t n = game_num_child_pos("000000000000__", i, &board);
//        if (n) printf("[%d: %d] ", i, n);
//        else printf("\n[%d: %d]\n", i, n);
//    }
    solve_local(1, 1ULL, 2ULL << 30);
//    game_test_sanity();
    return 0;
}
