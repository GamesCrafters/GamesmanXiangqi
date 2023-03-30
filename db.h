#ifndef DB_H
#define DB_H
#include <stdint.h>
#include <stdio.h>

#define DB_TIER_OK 0
#define DB_TIER_MISSING 1
#define DB_TIER_STAT_CORRUPTED 2

typedef struct TierSolverStat {
    uint64_t numLegalPos;
    uint64_t numWin;
    uint64_t numLose;
    uint64_t longestNumStepsToRedWin;
    uint64_t longestPosToRedWin;
    uint64_t longestNumStepsToBlackWin;
    uint64_t longestPosToBlackWin;
} tier_solver_stat_t;

uint16_t db_get_value(const char *tier, uint64_t hash);
FILE *db_fopen_tier(const char *tier, const char *modes);
FILE *db_fopen_stat(const char *tier, const char *modes);
int db_check_tier(const char *tier);
tier_solver_stat_t db_load_stat(const char *tier);

#endif // DB_H