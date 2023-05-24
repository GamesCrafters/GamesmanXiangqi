#include <stdio.h>
#include <stdlib.h>
#include "solver.h"

int main(int argc, char **argv) {
    // if (argc != 3) {
    //     printf("Usage: %s <tier-to-solve> <memory-in-GiB>.\n", argv[0]);
    //     return 1;
    // }

    // solve_local_single_tier(argv[1], (uint64_t)atoi(argv[2]) << 30);
    // solve_local_remaining_pieces(4, 24, 2ULL << 30, false);
    solve_local_from_file("../test", 2ULL << 30);
    return 0;
}
