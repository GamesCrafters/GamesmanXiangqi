#include "misc.h"
#include "solver.h"
#include "tier.h"
#include "tiersolver.h"
#include "tiertree.h"
#include <stdio.h>

// TODO
void solve_local(uint8_t nPiecesMax, uint64_t nthread, uint64_t mem) {
    TierTreeEntryList *solvableTiers = tier_tree_init(nPiecesMax, nthread << 5);

    TierTreeEntryList *tmp;
    while (solvableTiers) {
        solver_stat_t stat = solve_tier(solvableTiers->tier, nthread, mem);
        if (stat.numLegalPos) {
            /* Solve succeeded. Update tier tree. */

        }
        tmp = solvableTiers;
        solvableTiers = solvableTiers->next;
        free(tmp);
    }
    printf("solve_local: solver done.");
}


//printf("total solvable tiers with a maximum of %d pieces: %"PRIu64"\n",
//       nPiecesMax, tierCount96GiB+tierCount384GiB+tierCount1536GiB);
//printf("number of tiers that fit in 96 GiB memory: %"PRIu64"\n", tierCount96GiB);
//printf("number of tiers that fit in 384 GiB memory: %"PRIu64"\n", tierCount384GiB);
//printf("number of tiers that fit in 1536 GiB memory: %"PRIu64"\n", tierCount1536GiB);
//printf("number of tiers ignored: %"PRIu64"\n", tierCountIgnored);
//printf("max solvable tier size: %"PRIu64"\n", maxTierSize);
//printf("total size of all solvable tiers: %"PRIu64"\n", tierSizeTotal);














