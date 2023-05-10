#ifndef SOLVERMPI_H
#define SOLVERMPI_H
#include <stdbool.h>
#include <stdint.h>

void solve_mpi_manager(uint8_t nPiecesMax, uint64_t nthread);
void solve_mpi_worker(uint64_t mem, bool force);

#endif // SOLVERMPI_H
