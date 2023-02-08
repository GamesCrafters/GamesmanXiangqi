#include "misc.h"
#include "tiertree.h"
#include "types.h"
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/*************************** Global Constants ***************************/
/* Max number of remaining pieces of each type. */
static const char *REM_MAX = "222255222222";
/* Precalculated hash table number of buckets based on number of tiers. */
static const uint64_t DEFAULT_BUCKETS[14] = {
    0ULL, 89921753ULL, 89921753ULL, 89921753ULL, 89921753ULL,
    89921753ULL, 89921753ULL, 89921753ULL, 89921753ULL, 89921753ULL,
    89921753ULL, 243108433ULL, 593756447ULL, 1312600687ULL
};
/************************* End Global Constants *************************/

/*************************** Global Variables ***************************/
static tier_tree_entry_t **tree = NULL;
static uint64_t nbuckets = 0ULL;
static uint64_t nelements = 0ULL;
static pthread_mutex_t tree_lock;
static pthread_mutex_t solvableLock;
/************************* End Global Variables *************************/

/********************* Helper Function Declarations *********************/
static uint64_t strhash(const char *str);
static void tier_tree_add(const char *tier, uint8_t nChildren);
static void tier_tree_add_multithreaded(const char *tier, uint8_t nChildren);
static void solvable_list_add(const char *tier, TierTreeEntryList **solvable);
/******************* End Helper Function Declarations *******************/

/******************************* Tree Builder **********************************/

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

static void tier_tree_build_tree(int nPiecesMax) {
    char tier[TIER_STR_LENGTH_MAX] = "000000000000"; // 12 digits.
    /* 3^10 * 6^2 = 2125764 possible sets of remaining pieces on the board. */
    for (int i = 0; i < 2125764; ++i) {
        generate_tiers(tier, nPiecesMax, false);
        next_rem(tier);
    }
    printf("total number of buckets: %"PRIu64"\n", nbuckets);
    printf("total number of elements: %"PRIu64"\n", nelements);

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

static void append_black_pawns_multithread(char *tier, TierTreeEntryList **solvable) {
    int begin = 14 + tier[RED_P_IDX] - '0';
    int nump = tier[BLACK_P_IDX] - '0';
    tier[begin - 1] = '_';
    for (int i = 0; i < nump; ++i) {
        tier[begin + i] = '0';
    }
    tier[begin + nump] = '\0';
    while (true) {
        uint8_t numChildren = tier_num_child_tiers(tier);
        if (numChildren) {
            /* Add tier to tier tree if it depends on at least one child tier. */
            tier_tree_add_multithreaded(tier, tier_num_child_tiers(tier));
        } else {
            /* Tier is primitive and can be solved immediately. */
            solvable_list_add(tier, solvable);
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

static void append_red_pawns_multithread(char *tier, TierTreeEntryList **solvable) {
    tier[12] = '_';
    int numP = tier[RED_P_IDX] - '0';
    for (int i = 0; i < numP; ++i) {
        tier[13 + i] = '0';
    }
    while (true) {
        append_black_pawns_multithread(tier, solvable);
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

static void generate_tiers_multithread(char *tier, int nPiecesMax, TierTreeEntryList **solvable) {
    /* Do not include tiers that exceed maximum
       number of pieces on board. */
    int count = 0;
    for (int i = 0; i < 12; ++i) {
        count += tier[i] - '0';
    }
    /* Do not consider tiers that have more pieces
       than allowed on the board. */
    if (count > nPiecesMax) return;
    append_red_pawns_multithread(tier, solvable);
}

typedef struct TTBTMHelperArgs {
    uint64_t begin;
    uint64_t end;
    char **tiers;
    TierTreeEntryList **solvable;
    int nPiecesMax;
} ttbtm_helper_args_t;

static void *ttbtm_helper(void *_args) {
    ttbtm_helper_args_t *args = (ttbtm_helper_args_t*)_args;
    for (uint64_t i = args->begin; i < args->end; ++i) {
        generate_tiers_multithread(args->tiers[i], args->nPiecesMax, args->solvable);
    }
    pthread_exit(NULL);
    return NULL;
}

static TierTreeEntryList *tier_tree_build_tree_multithread(int nPiecesMax, uint64_t nthread) {
    TierTreeEntryList *solvable = NULL;
    char tier[TIER_STR_LENGTH_MAX] = "000000000000";
    const uint64_t GLOBAL_NUM_REMS = 2125764ULL;
    char **tiers = (char**)safe_calloc(GLOBAL_NUM_REMS, sizeof(char*));
    for (uint64_t i = 0; i < GLOBAL_NUM_REMS; ++i) {
        tiers[i] = (char*)safe_malloc(TIER_STR_LENGTH_MAX);
        for (int j = 0; j < TIER_STR_LENGTH_MAX; ++j) {
            tiers[i][j] = tier[j];
        }
        next_rem(tier);
    }

    pthread_t *tid = (pthread_t*)safe_calloc(nthread, sizeof(pthread_t*));
    ttbtm_helper_args_t *args = (ttbtm_helper_args_t*)safe_malloc(
                nthread * sizeof(ttbtm_helper_args_t)
                );
    pthread_mutex_init(&tree_lock, NULL);
    pthread_mutex_init(&solvableLock, NULL);

    for (uint64_t i = 0; i < nthread; ++i) {
        args[i].begin = i * (GLOBAL_NUM_REMS / nthread);
        args[i].end = (i == nthread - 1) ?
                    GLOBAL_NUM_REMS :
                    (i + 1) * (GLOBAL_NUM_REMS / nthread);
        args[i].tiers = tiers;
        args[i].nPiecesMax = nPiecesMax;
        args[i].solvable = &solvable;
        pthread_create(tid + i, NULL, ttbtm_helper, (void*)(args + i));
    }
    for (uint64_t i = 0; i < nthread; ++i) {
        pthread_join(tid[i], NULL);
    }
    free(args);
    free(tid);
    for (uint64_t i = 0; i < GLOBAL_NUM_REMS; ++i) {
        free(tiers[i]);
    }
    free(tiers);
    pthread_mutex_destroy(&tree_lock);
    pthread_mutex_destroy(&solvableLock);

    printf("tier_tree_build_tree_multithread: tier tree built.\n");
    printf("total number of buckets: %"PRIu64"\n", nbuckets);
    printf("total number of elements: %"PRIu64"\n", nelements);
    printf("solvable tiers: ");
    int solvableCount = 0;
    TierTreeEntryList *walker;
    for (walker = solvable; walker; walker = walker->next) {
        ++solvableCount;
        printf("[%s] ", walker->tier);
    }
    printf("\n");
    return solvable;
}

/********************** End Tree Builder Multithreaded ***********************/

/**************************** Tree Utilities *******************************/

/**
 * @brief Initilizes and builds the entire tier tree. Does nothing
 * if tier tree has already been initialized.
 */
TierTreeEntryList *tier_tree_init(uint8_t nPiecesMax, uint64_t nthread) {
    TierTreeEntryList *solvable = NULL;
    if (tree) return solvable;
    tree = safe_calloc(DEFAULT_BUCKETS[nPiecesMax], sizeof(tier_tree_entry_t*));
    nbuckets = DEFAULT_BUCKETS[nPiecesMax];
    return tier_tree_build_tree_multithread(nPiecesMax, nthread);
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

/**
 * @brief Adds a new tier into the tier tree. Note that this function
 * does not check for existing tiers. Therefore, adding an existing
 * tier again results in undefined behavior.
 */
static void tier_tree_add(const char *tier, uint8_t nChildren) {
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
 * @brief Adds a new tier into the tier tree. Note that this function
 * does not check for existing tiers. Therefore, adding an existing
 * tier again results in undefined behavior.
 */
static void tier_tree_add_multithreaded(const char *tier, uint8_t nChildren) {
    uint64_t slot = strhash(tier) % nbuckets;
    tier_tree_entry_t *e = safe_malloc(sizeof(tier_tree_entry_t));
    for (int i = 0; i < TIER_STR_LENGTH_MAX; ++i) {
        e->tier[i] = tier[i];
    }
    e->numUnsolvedChildren = nChildren;
    pthread_mutex_lock(&tree_lock);
    e->next = tree[slot];
    tree[slot] = e;
    ++nelements;
    pthread_mutex_unlock(&tree_lock);
}

static void solvable_list_add(const char *tier, TierTreeEntryList **solvable) {
    tier_tree_entry_t *e = safe_malloc(sizeof(tier_tree_entry_t));
    for (int i = 0; i < TIER_STR_LENGTH_MAX; ++i) {
        e->tier[i] = tier[i];
    }
    e->numUnsolvedChildren = 0;
    pthread_mutex_lock(&solvableLock);
    e->next = *solvable;
    *solvable = e;
    pthread_mutex_unlock(&solvableLock);
}
/***************************** End Helper Functions ******************************/
