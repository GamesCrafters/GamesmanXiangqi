#ifndef TIERSOLVER_H
#define TIERSOLVER_H
#include <stdbool.h>
#include <stdint.h>
#include "db.h"

tier_solver_stat_t tiersolver_solve_tier(const char *tier, uint64_t mem, bool force);

#endif // TIERSOLVER_H
