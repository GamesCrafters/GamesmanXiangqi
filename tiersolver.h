#ifndef TIERSOLVER_H
#define TIERSOLVER_H
#include <stdint.h>

typedef struct SolverStat {
    uint64_t numLegalPos;
    uint64_t numWin;
    uint64_t numLose;
    uint64_t longestNumStepsToRedWin;
    uint64_t longestPosToRedWin;
    uint64_t longestNumStepsToBlackWin;
    uint64_t longestPosToBlackWin;
} solver_stat_t;

solver_stat_t solve_tier(const char *tier, uint64_t nthread, uint64_t mem);

#endif // TIERSOLVER_H
