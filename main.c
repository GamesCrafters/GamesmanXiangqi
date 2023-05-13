#include "solver.h"
#include "common.h"
#include "game.h"
#include "db.h"

// TODO: double check all malloc failures.
int main() {
    make_triangle();
    solve_local(3, 1ULL, 2ULL << 30, false);
    return 0;
}
