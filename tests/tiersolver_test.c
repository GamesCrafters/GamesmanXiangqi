#include "tiersolver_test.h"
#include "../tiersolver.h"
#include <inttypes.h>
#include <stdio.h>
#include <sys/time.h>

void tiersolver_test_solve_single_tier(const char *tier) {
    struct timeval start_time, end_time;
    double elapsed_time;
    gettimeofday(&start_time, NULL); // record start time
    tier_solver_stat_t stat = tiersolver_solve_tier(tier, 90ULL << 30, true);
    gettimeofday(&end_time, NULL); // record end time
    elapsed_time = (end_time.tv_sec - start_time.tv_sec) * 1000000.0; // convert seconds to microseconds
    elapsed_time += (end_time.tv_usec - start_time.tv_usec); // add microseconds
    elapsed_time /= 1000000.0; // convert back to seconds
    
    printf("Tier %s:\n", tier);
    printf("total legal positions: %"PRIu64"\n", stat.numLegalPos);
    printf("number of winning positions: %"PRIu64"\n", stat.numWin);
    printf("number of losing positions: %"PRIu64"\n", stat.numLose);
    printf("number of drawing positions: %"PRIu64"\n", stat.numLegalPos - stat.numWin - stat.numLose);
    printf("longest win for red is %"PRIu64" steps at position %"PRIu64"\n", stat.longestNumStepsToRedWin, stat.longestPosToRedWin);
    printf("longest win for black is %"PRIu64" steps at position %"PRIu64"\n", stat.longestNumStepsToBlackWin, stat.longestPosToBlackWin);

    printf("Elapsed time: %f seconds\n", elapsed_time);
}
