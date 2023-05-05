#include "common.h"

uint64_t choose[CHOOSE_ROWS][CHOOSE_COLS];

void make_triangle(void) {
    int i, j;
    choose[0][0] = 1;
    for (i = 1; i < CHOOSE_ROWS; ++i) {
        choose[i][0] = 1;
        for (j = 1; j <= (i < CHOOSE_COLS-1 ? i : CHOOSE_COLS-1); ++j) {
            choose[i][j] = choose[i - 1][j - 1] + choose[i - 1][j];
        }
    }
}
