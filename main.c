#include <stdio.h>
#include "solver.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: %s <tier-to-solve>.\n", argv[0]);
        return 1;
    }

    solve_local_single_tier(argv[1], 2ULL << 30);
    return 0;
}
