#include "solver.h"
#include "common.h"
#include "game.h"
#include "db.h"

// TODO: double check all malloc failures.
// TODO: double check if all file pointers are closed.
int main() {
    make_triangle();
    solve_local(2, 1ULL, 2ULL << 30, true);
    return 0;
}
