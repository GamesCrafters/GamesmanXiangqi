#ifndef DB_H
#define DB_H
#include <stdint.h>
#include <stdio.h>

enum db_tier_file_status {
    DB_TIER_OK = 0,
    DB_TIER_MISSING,
    DB_TIER_STAT_CORRUPTED
};

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
int db_check_tier(const char *tier);

void db_save_tier(const char *tier, const uint16_t *values, uint64_t tierSize);
void db_save_stat(const char *tier, const tier_solver_stat_t stat);
uint16_t *db_load_tier(const char *tier, uint64_t tierSize);
tier_solver_stat_t db_load_stat(const char *tier);

#endif // DB_H
