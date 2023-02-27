#include "common.h"
//#include "tests/game_test.h"
#include <solver.h>

// TODO: replace for loops with memcpy.
// TODO: replace vector implementation mallocs with reallocs.
int main() {
    make_triangle();
    solve_local(1, 1ULL, 2ULL << 30);
//    game_test_sanity();
    return 0;
}
