#ifndef SOLVER_H
#define SOLVER_H
#include <stdbool.h>
#include <stdint.h>

void solve_local_remaining_pieces(uint8_t nPiecesMax, uint64_t nthread, uint64_t mem, bool force);
void count_local_remaining_pieces(uint8_t nPiecesMax, uint64_t nthread);
bool solve_local_single_tier(const char *tier, uint64_t mem);
void solve_local_from_file(const char *filename, uint64_t mem);

#endif // SOLVER_H
