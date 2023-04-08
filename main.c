#include "solver.h"
#include "common.h"
#include "game.h"
#include "db.h"
#include <inttypes.h>

// TODO: replace for loops with memcpy.
// TODO: double check all malloc failures.
int main() {
    make_triangle();
    solve_local(3, 1ULL, 2ULL << 30, false);
    return 0;
}
