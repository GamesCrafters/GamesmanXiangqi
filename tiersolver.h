#ifndef TIERSOLVER_H
#define TIERSOLVER_H
#include <stdbool.h>
#include <stdint.h>
#include "db.h"
#include "common.h"

tier_solver_stat_t tiersolver_solve_tier(const char *tier, uint64_t mem, bool force);
analysis_t tiersolver_count_tier(const char *tier, bool is_canonical);

#endif // TIERSOLVER_H
