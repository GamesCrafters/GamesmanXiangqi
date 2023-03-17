#include "tiersolver_test.h"
#include "../tiersolver.h"
#include <inttypes.h>
#include <stdio.h>

void tiersolver_test_solve_single_tier(const char *tier) {
    tier_solver_stat_t stat = solve_tier(tier, 1, 2ULL << 30);
    printf("Tier %s:\n", tier);
    printf("total legal positions: %"PRIu64"\n", stat.numLegalPos);
    printf("number of winning positions: %"PRIu64"\n", stat.numWin);
    printf("number of losing positions: %"PRIu64"\n", stat.numLose);
    printf("number of drawing positions: %"PRIu64"\n", stat.numLegalPos - stat.numWin - stat.numLose);
    printf("longest win for red is %"PRIu64" steps at position %"PRIu64"\n", stat.longestNumStepsToRedWin, stat.longestPosToRedWin);
    printf("longest win for black is %"PRIu64" steps at position %"PRIu64"\n", stat.longestNumStepsToBlackWin, stat.longestPosToBlackWin);
}
