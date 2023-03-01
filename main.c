#include "solver.h"
#include "common.h"
#include "game.h"
#include "db.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define OK       0
#define NO_INPUT 1
#define TOO_LONG 2
static int getLine (char *prmpt, char *buff, size_t sz) {
    int ch, extra;

    // Get line with buffer overrun protection.
    if (prmpt != NULL) {
        printf ("%s", prmpt);
        fflush (stdout);
    }
    if (fgets (buff, sz, stdin) == NULL)
        return NO_INPUT;

    // If it was too long, there'll be no newline. In that case, we flush
    // to end of line so that excess doesn't affect the next call.
    if (buff[strlen(buff)-1] != '\n') {
        extra = 0;
        while (((ch = getchar()) != '\n') && (ch != EOF))
            extra = 1;
        return (extra == 1) ? TOO_LONG : OK;
    }

    // Otherwise remove newline and give string back to caller.
    buff[strlen(buff)-1] = '\0';
    return OK;
}

static void query_forever(void) {
    board_t board;
    memset(board.layout, BOARD_EMPTY_CELL, 90);
    board.valid = true;

    char tier[25];
    char buff[25];
    uint64_t hash;
    while (true) {
        getLine("enter tier, or leave emtpy to use previous tier> ", buff, sizeof(buff));
        if (strlen(buff)) strncpy(tier, buff, 25);
        getLine("enter hash> ", buff, sizeof(buff));
        if (!strlen(buff)) return;
        hash = (uint64_t)atoll(buff);
        unhash(&board, tier, hash);
        print_board(&board);
        clear_board(&board);
        printf("[rmt(%d) in tier %s: %d]\n", hash, tier, db_get_value(tier, hash));
    }
}

// TODO: replace for loops with memcpy.
// TODO: replace vector implementation mallocs with reallocs.
int main() {
    make_triangle();
    // query_forever();

    // board_t board;
    // memset(board.layout, BOARD_EMPTY_CELL, 90);
    // board.valid = true;

    // printf("[%d]\n", game_num_child_pos("000010000000_2_", 263, &board));

    // for (uint64_t i = 0; i < 900; ++i) {
    //     printf("[%d: %d] ", i, game_num_child_pos("000001000000__0", i, &board));
    // }
   
    solve_local(2, 1ULL, 2ULL << 30);

    return 0;
}
