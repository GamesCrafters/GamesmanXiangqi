#ifndef TIERSOLVER_H
#define TIERSOLVER_H
#include <stdint.h>
#include "db.h"

tier_solver_stat_t solve_tier(const char *tier, uint64_t nthread, uint64_t mem);

#endif // TIERSOLVER_H
