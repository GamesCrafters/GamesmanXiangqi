#include "misc.h"
#include "tiertree.h"
#include "types.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/***************************** Settings *****************************/
static const bool VERBOSE = false;
static const double LOAD_FACTOR_MAX = 1.0;
static const uint64_t DEFAULT_BUCKETS = 127ULL;
/***************************** End Settings *****************************/

/*************************** Global Constants ***************************/
/* Max number of remaining pieces of each type. */
static const char *REM_MAX = "222255222222";
/************************* End Global Constants *************************/

/*************************** Global Variables ***************************/
static tier_tree_entry_t **tree = NULL;
static uint64_t nbuckets = 0ULL;
static uint64_t nelements = 0ULL;
/************************* End Global Variables *************************/

/* Statistics */
typedef struct TierStat {
    uint64_t maxTierSize;
    uint64_t tierSizeTotal;
    uint64_t tierCount96GiB;
    uint64_t tierCount384GiB;
    uint64_t tierCount1536GiB;
    uint64_t tierCountIgnored;
} tier_stat_t;

static uint64_t maxTierSize = 0ULL;
static uint64_t tierSizeTotal = 0ULL;
static uint64_t tierCount96GiB = 0ULL;
static uint64_t tierCount384GiB = 0ULL;
static uint64_t tierCount1536GiB = 0ULL;
static uint64_t tierCountIgnored = 0ULL;

/********************* Helper Function Declarations *********************/
static bool is_prime(uint64_t n);
static uint64_t next_prime(uint64_t n);
static uint64_t strhash(const char *str);
static uint64_t safe_add_uint64(uint64_t lhs, uint64_t rhs);
static uint64_t safe_mult_uint64(uint64_t lhs, uint64_t rhs);

static void tier_tree_add(const char *tier, uint8_t nChildren);
static void tier_tree_expand(void);
/******************* End Helper Function Declarations *******************/

/******************************* Tree Builder **********************************/

static void tally(const char *tier, bool verbose) {
    uint64_t size = tier_size(tier);
    /* Start counter at 1 since 0ULL is reserved for error.
       When calculations are done, we know that an error has
       occurred if this value is 0ULL. Otherwise, decrement
       this counter to get the actual value. */
    uint64_t childSizeTotal = 1ULL;
    uint64_t childSizeMax = 0ULL;
    uint64_t mem;
    bool overflow = false;
    bool feasible = false;

    TierList *childTiers = child_tiers(tier);
    for (struct TierListElem *curr = childTiers; curr; curr = curr->next) {
        uint64_t currChildSize = tier_size(curr->tier);
        childSizeTotal = safe_add_uint64(childSizeTotal, currChildSize);
        if (currChildSize > childSizeMax) {
            childSizeMax = currChildSize;
        }
    }
    free_tier_list(childTiers);

    if (size == 0ULL || childSizeTotal == 0ULL) {
        overflow = true;
        ++tierCountIgnored;
    } else {
        /* memory needed = 16*childSizeTotal + 19*size. */
        mem = safe_add_uint64(
                    safe_mult_uint64(19ULL, size),
                    safe_mult_uint64(16ULL, childSizeTotal)
                    );
        if (mem == 0ULL) {
            overflow = true;
            ++tierCountIgnored;
            /* We initialized childSizeTotal to 1, so we need to fix the calculation. */
        } else if ( (mem -= 16ULL) < (96ULL*(1ULL << 30)) ) { // fits in 96 GiB
            ++tierCount96GiB;
            feasible = true;
        } else if ( mem < (384ULL*(1ULL << 30)) ) { // fits in 384 GiB
            ++tierCount384GiB;
            feasible = true;
        } else if ( mem < (1536ULL*(1ULL << 30)) ) { // fits in 1536 GiB
            ++tierCount1536GiB;
            feasible = true;
        } else {
            ++tierCountIgnored;
        }
    }

    /* Update global tier counters only if we decide to solve current tier. */
    if (feasible) {
        if (size > maxTierSize) {
            maxTierSize = size;
        }
        tierSizeTotal += size;
    }

    /* Print out details only if the list is short. */
    if (verbose) {
        if (overflow) {
            printf("%s: overflow\n\n", tier);
        } else {
            /* We initialized childSizeTotal to 1, so we need to decrement it. */
            printf("%s: size == %"PRIu64", max child tier size == %"PRIu64","
                   " child tiers size total == %"PRIu64", MEM == %"PRIu64"B\n\n",
                   tier, size, childSizeMax, --childSizeTotal, mem);
        }
    }
}

static void append_black_pawns(char *tier, bool test) {
    int begin = 14 + tier[RED_P_IDX] - '0';
    int nump = tier[BLACK_P_IDX] - '0';
    tier[begin - 1] = '_';
    for (int i = 0; i < nump; ++i) {
        tier[begin + i] = '0';
    }
    tier[begin + nump] = '\0';
    while (true) {
        if (test) {
            tier_tree_entry_t *e = tier_tree_find(tier);
            assert(e);
            assert(e->numUnsolvedChildren == tier_num_child_tiers(tier));
            assert(tier_tree_remove(tier) == e);
            free(e);
            assert(!tier_tree_find(tier));
        } else {
            tally(tier, VERBOSE);
            tier_tree_add(tier, tier_num_child_tiers(tier));
        }
        /* Go to next combination. */
        int i = begin;
        ++tier[begin];
        while (tier[i] > '6' && i < begin + nump) {
            ++tier[++i];
        }
        if (i == begin + nump) {
            break;
        }
        for (int j = begin; j < i; ++j) {
            tier[j] = tier[i];
        }
    }
}

static void append_red_pawns(char *tier, bool test) {
    tier[12] = '_';
    int numP = tier[RED_P_IDX] - '0';
    for (int i = 0; i < numP; ++i) {
        tier[13 + i] = '0';
    }
    while (true) {
        append_black_pawns(tier, test);
        /* Go to next combination. */
        int i = 13;
        ++tier[13];
        while (tier[i] > '6' && i < 13 + numP) {
            ++tier[++i];
        }
        if (i == 13 + numP) {
            break;
        }
        for (int j = 13; j < i; ++j) {
            tier[j] = tier[i];
        }
    }
}

static void generate_tiers(char *tier, int nPiecesMax, bool test) {
    int count = 0;
    for (int i = 0; i < 12; ++i) {
        count += tier[i] - '0';
    }
    /* Do not consider tiers that have more pieces
       than allowed on the board. */
    if (count > nPiecesMax) return;
    append_red_pawns(tier, test);
}

static void next_rem(char *tier) {
    int i = 0;
    ++tier[0];
    while (tier[i] > REM_MAX[i]) {
        /* Carry. */
        tier[i++] = '0';
        if (i == 12) break;
        ++tier[i];
    }
}

void tier_tree_build_tree(int nPiecesMax) {
    char tier[TIER_STR_LENGTH_MAX] = "000000000000"; // 12 digits.
    /* 3^10 * 6^2 = 2125764 possible sets of remaining pieces on the board. */
    for (int i = 0; i < 2125764; ++i) {
        generate_tiers(tier, nPiecesMax, false);
        next_rem(tier);
    }
    printf("total solvable tiers with a maximum of %d pieces: %"PRIu64"\n",
           nPiecesMax, tierCount96GiB+tierCount384GiB+tierCount1536GiB);
    printf("number of tiers that fit in 96 GiB memory: %"PRIu64"\n", tierCount96GiB);
    printf("number of tiers that fit in 384 GiB memory: %"PRIu64"\n", tierCount384GiB);
    printf("number of tiers that fit in 1536 GiB memory: %"PRIu64"\n", tierCount1536GiB);
    printf("number of tiers ignored: %"PRIu64"\n", tierCountIgnored);
    printf("max solvable tier size: %"PRIu64"\n", maxTierSize);
    printf("total size of all solvable tiers: %"PRIu64"\n", tierSizeTotal);

    /* Self-test */
    char tier2[TIER_STR_LENGTH_MAX] = "000000000000";
    for (int i = 0; i < 2125764; ++i) {
        generate_tiers(tier2, nPiecesMax, true);
        next_rem(tier2);
    }
    assert(nelements == 0);
    printf("tier_tree_build_tree: self-test passed\n");
}

/***************************** End Tree Builder ********************************/

/************************* Tree Builder Multithreaded **************************/

//static void tally_multithread(char *tier, tier_stat_t *stat) {
//    uint64_t size = tier_size(tier);
//    /* Start counter at 1 since 0ULL is reserved for error.
//       When calculations are done, an error has occurred
//       if this value is 0ULL. Otherwise, decrement this counter
//       to get the actual value. */
//    uint64_t childSizeTotal = 1ULL;
//    uint64_t childSizeMax = 0ULL;
//    uint64_t mem;
//    bool overflow = false;
//    bool feasible = false;

//    TierList *childTiers = child_tiers(tier);
//    for (struct TierListElem *curr = childTiers; curr; curr = curr->next) {
//        uint64_t currChildSize = tier_size(curr->tier);
//        childSizeTotal = safe_add_uint64(childSizeTotal, currChildSize);
//        if (currChildSize > childSizeMax) {
//            childSizeMax = currChildSize;
//        }
//    }
//    free_tier_list(childTiers);

//    if (size == 0ULL || childSizeTotal == 0ULL) {
//        overflow = true;
//        ++stat->tierCountIgnored;
//    } else {
//        /* memory needed = 16*childSizeTotal + 19*size. */
//        mem = safe_add_uint64(
//                    safe_mult_uint64(19ULL, size),
//                    safe_mult_uint64(16ULL, childSizeTotal)
//                    );
//        if (mem == 0ULL) {
//            overflow = true;
//            ++stat->tierCountIgnored;
//            /* We initialized childSizeTotal to 1, so we need to fix the calculation. */
//        } else if ( (mem -= 16ULL) < (96ULL*(1ULL << 30)) ) { // fits in 96 GiB
//            ++stat->tierCount96GiB;
//            feasible = true;
//        } else if ( mem < (384ULL*(1ULL << 30)) ) { // fits in 384 GiB
//            ++stat->tierCount384GiB;
//            feasible = true;
//        } else if ( mem < (1536ULL*(1ULL << 30)) ) { // fits in 1536 GiB
//            ++stat->tierCount1536GiB;
//            feasible = true;
//        } else {
//            ++stat->tierCountIgnored;
//        }
//    }

//    /* Update global tier counters only if we decide to solve current tier. */
//    if (feasible) {
//        if (size > stat->maxTierSize) {
//            stat->maxTierSize = size;
//        }
//        stat->tierSizeTotal += size;
//    }

//    /* Print out details only if the list is short. */
//    if (END_GAME_PIECES_MAX < 4) {
//        if (overflow) {
//            printf("%s: overflow\n", tier);
//        } else {
//            /* We initialized childSizeTotal to 1, so we need to decrement it. */
//            printf("%s: size == %"PRIu64", max child tier size == %"PRIu64","
//                   " child tiers size total == %"PRIu64", MEM == %"PRIu64"B\n",
//                   tier, size, childSizeMax, --childSizeTotal, mem);
//        }
//    } else {
//        (void)overflow; // suppress unused variable warning.
//    }
//}

//static void append_black_pawns_multithread(char *tier, tier_stat_t *stat) {
//    int begin = 14 + tier[RED_P_IDX] - '0';
//    int nump = tier[BLACK_P_IDX] - '0';
//    tier[begin - 1] = '_';
//    for (int i = 0; i < nump; ++i) {
//        tier[begin + i] = '0';
//    }
//    tier[begin + nump] = '\0';
//    while (true) {
//        tally_multithread(tier, stat);
//        /* Go to next combination. */
//        int i = begin;
//        ++tier[begin];
//        while (tier[i] > '6' && i < begin + nump) {
//            ++tier[++i];
//        }
//        if (i == begin + nump) {
//            break;
//        }
//        for (int j = begin; j < i; ++j) {
//            tier[j] = tier[i];
//        }
//    }
//}

//static void append_red_pawns_multithread(char *tier, tier_stat_t *stat) {
//    tier[12] = '_';
//    int numP = tier[RED_P_IDX] - '0';
//    for (int i = 0; i < numP; ++i) {
//        tier[13 + i] = '0';
//    }
//    while (true) {
//        append_black_pawns_multithread(tier, stat);
//        /* Go to next combination. */
//        int i = 13;
//        ++tier[13];
//        while (tier[i] > '6' && i < 13 + numP) {
//            ++tier[++i];
//        }
//        if (i == 13 + numP) {
//            break;
//        }
//        for (int j = 13; j < i; ++j) {
//            tier[j] = tier[i];
//        }
//    }
//}

//static void generate_tiers_multithread(char *tier, tier_stat_t *stat) {
//    /* Do not include tiers that exceed maximum
//       number of pieces on board. */
//    int count = 0;
//    for (int i = 0; i < 12; ++i) {
//        count += tier[i] - '0';
//    }
//    /* Do not consider tiers that have more pieces
//       than allowed on the board. */
//    if (count > END_GAME_PIECES_MAX) return;
//    append_red_pawns_multithread(tier, stat);
//}

//typedef struct TDMHelperArgs {
//    uint64_t begin;
//    uint64_t end;
//    char **tiers;
//} tdm_helper_args_t;

//static void *tdm_helper(void *_args) {
//    tdm_helper_args_t *args = (tdm_helper_args_t*)_args;
//    tier_stat_t *stat = (tier_stat_t*)calloc(1, sizeof(tier_stat_t));
//    for (uint64_t i = args->begin; i < args->end; ++i) {
//        generate_tiers_multithread(args->tiers[i], stat);
//    }
//    pthread_exit(stat);
//    return NULL;
//}

//void tier_driver_multithread(uint64_t nthread) {
//    make_triangle();
//    char tier[TIER_STR_LENGTH_MAX] = "000000000000";
//    uint64_t num_tiers = 2125764ULL;
//    char **tiers = (char**)safe_calloc(num_tiers, sizeof(char*));
//    for (uint64_t i = 0; i < num_tiers; ++i) {
//        tiers[i] = (char*)safe_malloc(TIER_STR_LENGTH_MAX);
//        for (int j = 0; j < TIER_STR_LENGTH_MAX; ++j) {
//            tiers[i][j] = tier[j];
//        }
//        next_rem(tier);
//    }

//    pthread_t *tid = (pthread_t*)safe_calloc(nthread, sizeof(pthread_t*));
//    tdm_helper_args_t *args = (tdm_helper_args_t*)safe_calloc(nthread, sizeof(tdm_helper_args_t));

//    for (uint64_t i = 0; i < nthread; ++i) {
//        args[i].begin = i * (num_tiers / nthread);
//        args[i].end = (i == nthread - 1) ? num_tiers : (i + 1) * (num_tiers / nthread);
//        args[i].tiers = tiers;
//        pthread_create(tid + i, NULL, tdm_helper, (void*)(args + i));
//    }
//    for (uint64_t i = 0; i < nthread; ++i) {
//        tier_stat_t *stat;
//        pthread_join(tid[i], (void**)&stat);
//        /* Update global statistics. */
//        tierCount1536GiB += stat->tierCount1536GiB;
//        tierCount384GiB += stat->tierCount384GiB;
//        tierCount96GiB += stat->tierCount96GiB;
//        tierCountIgnored += stat->tierCountIgnored;
//        tierSizeTotal += stat->tierSizeTotal;
//        if (stat->maxTierSize > maxTierSize) {
//            maxTierSize = stat->maxTierSize;
//        }
//        free(stat);
//    }
//    free(args);
//    free(tid);
//    for (uint64_t i = 0; i < num_tiers; ++i) {
//        free(tiers[i]);
//    }
//    free(tiers);

//    printf("total solvable tiers with a maximum of %d pieces: %"PRIu64"\n",
//           END_GAME_PIECES_MAX, tierCount96GiB+tierCount384GiB+tierCount1536GiB);
//    printf("number of tiers that fit in 96 GiB memory: %"PRIu64"\n", tierCount96GiB);
//    printf("number of tiers that fit in 384 GiB memory: %"PRIu64"\n", tierCount384GiB);
//    printf("number of tiers that fit in 1536 GiB memory: %"PRIu64"\n", tierCount1536GiB);
//    printf("number of tiers ignored: %"PRIu64"\n", tierCountIgnored);
//    printf("max solvable tier size: %"PRIu64"\n", maxTierSize);
//    /* Although in theory, this tierSizeTotal value could overflow, but it's unlikely.
//       Not checking for now. */
//    printf("total size of all solvable tiers: %"PRIu64"\n", tierSizeTotal);
//}

/********************** End Tree Builder Multithreaded ***********************/

/**************************** Tree Utilities *******************************/

/**
 * @brief Initilizes and builds the entire tier tree. Does nothing
 * if tier tree has already been initialized.
 */
void tier_tree_init(uint8_t nPiecesMax) {
    if (tree) return;
    tree = safe_calloc(DEFAULT_BUCKETS, sizeof(tier_tree_entry_t*));
    nbuckets = DEFAULT_BUCKETS;
    tier_tree_build_tree(nPiecesMax);
}

/**
 * @brief Deallocates tier tree. Does nothing if tier tree
 * has not been initialized.
 */
void tier_tree_destroy(void) {
    if (!tree) return;
    for (uint64_t i = 0; i < nbuckets; ++i) {
        tier_tree_entry_t *walker = tree[i];
        tier_tree_entry_t *next;
        while (walker) {
            next = walker->next;
            free(walker);
            walker = next;
        }
    }
    free(tree);
    tree = NULL;
    nbuckets = 0ULL;
    nelements = 0ULL;
}

/**
 * @brief Returns the tier tree entry corresponding to TIER.
 * Returns NULL if not found.
 */
tier_tree_entry_t *tier_tree_find(const char *tier) {
    uint64_t slot = strhash(tier) % nbuckets;
    tier_tree_entry_t *walker = tree[slot];
    while (walker && strcmp(walker->tier, tier)) {
        walker = walker->next;
    }
    return walker;
}

/**
 * @brief Removes and returns the tier tree entry corresponding to
 * TIER. Nothing is removed and NULL is returned if the given TIER
 * is not found.
 */
tier_tree_entry_t *tier_tree_remove(const char *tier) {
    uint64_t slot = strhash(tier) % nbuckets;
    tier_tree_entry_t **walker = tree + slot;
    while (*walker && strcmp((*walker)->tier, tier)) {
        walker = &((*walker)->next);
    }
    if (!(*walker)) {
        return NULL;
    }
    tier_tree_entry_t *ret = *walker;
    *walker = (*walker)->next;
    --nelements;
    return ret;
}

/**************************** End Tree Utilities *******************************/

/***************************** Helper Functions ******************************/

/**
 * @brief Returns true if N is prime, false otherwise.
 * 0 and 1 are not primes by convention.
 */
static bool is_prime(uint64_t n) {
    if (n <= 1) return false;
    if (n <= 3) return true;
    if (n%2 == 0 || n%3 == 0) return false;
    uint64_t i;
    for (i = 5; i*i <= n; i += 6) {
        if (n%i == 0 || n%(i + 2) == 0) {
           return false;
        }
    }
    return true;
}

/**
 * @brief Returns the smallest prime number that is greater than
 * or equal to N, assuming no integer overflow will ever
 * occur.
 */
static uint64_t next_prime(uint64_t n) {
    /* Returns the smallest prime number that is greater than
       or equal to N, assuming no integer overflow will ever
       occur. */
    while (!is_prime(n)) ++n;
    return n;
}

/**
 * @brief Returns the 64-bit hash of a string.
 * @author Dan Bernstein, http://www.cse.yorku.ca/~oz/hash.html
 */
static uint64_t strhash(const char *str) {
    uint64_t hash = 5381ULL;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

static uint64_t safe_add_uint64(uint64_t lhs, uint64_t rhs) {
    if (!lhs || !rhs || lhs > UINT64_MAX - rhs) {
        return 0;
    }
    return lhs + rhs;
}

static uint64_t safe_mult_uint64(uint64_t lhs, uint64_t rhs) {
    if (!lhs || !rhs || lhs > UINT64_MAX / rhs) {
        return 0;
    }
    return lhs * rhs;
}

/**
 * @brief Adds a new tier into the tier tree. Note that this function
 * does not check for existing tiers. Therefore, adding an existing
 * tier again results in undefined behavior.
 */
static void tier_tree_add(const char *tier, uint8_t nChildren) {
    if ((double)nelements / nbuckets > LOAD_FACTOR_MAX) {
        tier_tree_expand();
    }
    uint64_t slot = strhash(tier) % nbuckets;
    tier_tree_entry_t *e = safe_malloc(sizeof(tier_tree_entry_t));
    e->next = tree[slot];
    for (int i = 0; i < TIER_STR_LENGTH_MAX; ++i) {
        e->tier[i] = tier[i];
    }
    e->numUnsolvedChildren = nChildren;
    tree[slot] = e;
    ++nelements;
}

/**
 * @brief Expands the tier tree hash map by doubling the
 * number of buckets.
 */
static void tier_tree_expand(void) {
    uint64_t oldNbuckets = nbuckets;
    tier_tree_entry_t **oldTierTree = tree;
    nbuckets = next_prime(nbuckets << 1);
    tree = safe_calloc(nbuckets, sizeof(tier_tree_entry_t*));
    nelements = 0ULL;
    tier_tree_entry_t *walker, *next;
    for (uint64_t i = 0; i < oldNbuckets; ++i) {
        walker = oldTierTree[i];
        while (walker) {
            tier_tree_add(walker->tier, walker->numUnsolvedChildren);
            next = walker->next;
            free(walker);
            walker = next;
        }
    }
    free(oldTierTree);
}
/***************************** End Helper Functions ******************************/
