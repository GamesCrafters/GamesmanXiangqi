#include "misc.h"
#include "tiertree.h"
#include "common.h"
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*************************** Global Constants ***************************/
/* 3^10 * 6^2 = 2125764 possible sets of remaining pieces on the board. */
#define N_REMS 2125764
/* Max number of remaining pieces of each type. */
static const char *REM_MAX = "222255222222";
/* Precalculated hash table number of buckets based on number of tiers. */
static const uint64_t DEFAULT_BUCKETS[14] = {
    2ULL, 29ULL, 331ULL, 2917ULL, 20231ULL,
    155509ULL, 562739ULL, 2389259ULL, 8961619ULL, 29967629ULL,
    89921753ULL, 243108433ULL, 593756447ULL, 1312600687ULL
};
/************************* End Global Constants *************************/

/*************************** Global Variables ***************************/
static tier_tree_entry_t **tree = NULL;
static uint64_t nbuckets = 0ULL;
static uint64_t nelements = 0ULL;
static pthread_mutex_t treeLock;
static pthread_mutex_t solvableLock;
/************************* End Global Variables *************************/

/********************* Helper Function Declarations *********************/
static void next_rem(char *tier);
static uint64_t strhash(const char *str);
static void tier_tree_add(const char *tier, uint8_t nChildren, pthread_mutex_t *treeLock);
static void solvable_list_add(const char *tier, TierTreeEntryList **solvable, pthread_mutex_t *solvableLock);
static void print_tier_tree_status(TierTreeEntryList *solvable);
/******************* End Helper Function Declarations *******************/

/******************************* Tier Scanner **********************************/

static void append_black_pawns(char *tier, void (*func)(const char*)) {
    int begin = 14 + tier[RED_P_IDX] - '0';
    int nump = tier[BLACK_P_IDX] - '0';
    tier[begin - 1] = '_';
    for (int i = 0; i < nump; ++i) {
        tier[begin + i] = '0';
    }
    tier[begin + nump] = '\0';
    while (true) {
        if (func && tier_is_legal_tier(tier)) func(tier);
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

static void append_red_pawns(char *tier, void (*func)(const char*)) {
    tier[12] = '_';
    int numP = tier[RED_P_IDX] - '0';
    for (int i = 0; i < numP; ++i) {
        tier[13 + i] = '0';
    }
    while (true) {
        append_black_pawns(tier, func);
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

static void generate_tiers(char *tier, int nPiecesMax, void (*func)(const char*)) {
    int count = 0;
    for (int i = 0; i < 12; ++i) {
        count += tier[i] - '0';
    }
    /* Do not consider tiers that have more pieces
       than allowed on the board. */
    if (count > nPiecesMax) return;
    append_red_pawns(tier, func);
}

void tier_scan_driver(int nPiecesMax, void (*func)(const char*)) {
    char tier[TIER_STR_LENGTH_MAX] = "000000000000"; // 12 digits.
    for (int i = 0; i < N_REMS; ++i) {
        generate_tiers(tier, nPiecesMax, func);
        next_rem(tier);
    }
}

/***************************** End Tier Scanner ********************************/

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
        uint8_t numChildren = tier_num_canonical_child_tiers(tier);

        /* Add tier to tier tree if it depends on at least one child tier. */
        if (numChildren) tier_tree_add(tier, numChildren, &treeLock);
        /* Tier is primitive and can be solved immediately. */
        else solvable_list_add(tier, solvable, &solvableLock);

        /* Go to next combination. */
        int i = begin;
        ++tier[begin];
        while (tier[i] > '6' && i < begin + nump) ++tier[++i];
        if (i == begin + nump) break;
        for (int j = begin; j < i; ++j) tier[j] = tier[i];
    }
}

static void append_red_pawns_multithread(char *tier, TierTreeEntryList **solvable) {
    tier[12] = '_';
    int numP = tier[RED_P_IDX] - '0';
    for (int i = 0; i < numP; ++i) tier[13 + i] = '0';
    while (true) {
        append_black_pawns_multithread(tier, solvable);
        /* Go to next combination. */
        int i = 13;
        ++tier[13];
        while (tier[i] > '6' && i < 13 + numP) ++tier[++i];
        if (i == 13 + numP) break;
        for (int j = 13; j < i; ++j) tier[j] = tier[i];
    }
}

static void generate_tiers_multithread(char *tier, int nPiecesMax, TierTreeEntryList **solvable) {
    /* Do not include tiers that exceed maximum
       number of pieces on board. */
    int count = 0;
    for (int i = 0; i < 12; ++i) count += tier[i] - '0';
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

static void *btm_helper(void *_args) {
    ttbtm_helper_args_t *args = (ttbtm_helper_args_t*)_args;
    for (uint64_t i = args->begin; i < args->end; ++i) {
        generate_tiers_multithread(args->tiers[i], args->nPiecesMax, args->solvable);
    }
    pthread_exit(NULL);
    return NULL;
}

static TierTreeEntryList *build_tree_multithread(int nPiecesMax, uint64_t nthread) {
    TierTreeEntryList *solvable = NULL;
    char tier[TIER_STR_LENGTH_MAX] = "000000000000";
    char **tiers = (char**)safe_calloc(N_REMS, sizeof(char*));
    for (uint64_t i = 0; i < N_REMS; ++i) {
        tiers[i] = (char*)safe_malloc(TIER_STR_LENGTH_MAX);
        memcpy(tiers[i], tier, TIER_STR_LENGTH_MAX);
        next_rem(tier);
    }
    
    pthread_t *tid = (pthread_t*)safe_calloc(nthread, sizeof(pthread_t*));
    ttbtm_helper_args_t *args = (ttbtm_helper_args_t*)safe_malloc(
                nthread * sizeof(ttbtm_helper_args_t));
    pthread_mutex_init(&treeLock, NULL);
    pthread_mutex_init(&solvableLock, NULL);

    for (uint64_t i = 0; i < nthread; ++i) {
        args[i].begin = i * (N_REMS / nthread);
        args[i].end = (i == nthread - 1) ? N_REMS : (i + 1) * (N_REMS / nthread);
        args[i].tiers = tiers;
        args[i].nPiecesMax = nPiecesMax;
        args[i].solvable = &solvable;
        pthread_create(tid + i, NULL, btm_helper, (void*)(args + i));
    }
    for (uint64_t i = 0; i < nthread; ++i) pthread_join(tid[i], NULL);
    free(args);
    free(tid);
    for (uint64_t i = 0; i < N_REMS; ++i) free(tiers[i]);
    free(tiers);
    pthread_mutex_destroy(&treeLock);
    pthread_mutex_destroy(&solvableLock);

    printf("build_tree_multithread: tier tree built.\n");
    print_tier_tree_status(solvable);
    return solvable;
}

/********************** End Tree Builder Multithreaded ***********************/

/************************* File-based Tree Builder ***************************/

static void add_tier_recursive(const char *tier, TierTreeEntryList **solvable) {
    /* Convert tier to canonical. */
    struct TierListElem *canonical = tier_get_canonical_tier(tier);
    if (!canonical) {
        printf("build_tree_from_file: OOM.\n");
        exit(1);
    }

    /* Return if the given tier has already been added. This means all
       of its child tiers have also been added. */
    if (tier_tree_find(canonical->tier)) {
        free(canonical);
        return;
    }

    /* Add the given tier to the tier tree. */
    uint8_t numChildren = tier_num_canonical_child_tiers(canonical->tier);
    if (numChildren) tier_tree_add(canonical->tier, numChildren, NULL);
    else solvable_list_add(canonical->tier, solvable, NULL);

    /* Recursively add all of its child tiers. */
    struct TierArray childTiers = tier_get_child_tier_array(canonical->tier); // If OOM, there is a bug.
    free(canonical); canonical = NULL;
    for (uint8_t i = 0; i < childTiers.size; ++i) {
        add_tier_recursive(childTiers.tiers[i], solvable);
    }
    tier_array_destroy(&childTiers);
}

static TierTreeEntryList *build_tree_from_file(const char *filename, uint64_t mem) {
    TierTreeEntryList *solvable = NULL;
    char tier[TIER_STR_LENGTH_MAX];
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf("tier_tree_init_from_file: failed to open file %s.\n", filename);
        return NULL;
    }
    while (fgets(tier, TIER_STR_LENGTH_MAX, f)) {
        tier[strlen(tier) - 1] = '\0'; // Get rid of '\n'.
        uint64_t reqMem = tier_required_mem(tier);
        if (!tier_is_legal_tier(tier)) {
            printf("tier_tree_init_from_file: skipping illegal tier %s.\n",
                   tier);
        } else if (reqMem == 0ULL) {
            printf("tier_tree_init_from_file: skipping tier %s, which "
                   "requires an amount of memory that cannot be "
                   "expressed as a 64-bit unsigned integer.\n", tier);
        } else if (mem && reqMem > mem) {
            printf("tier_tree_init_from_file: skipping tier %s, which "
                   "requires %"PRIu64" bytes of memory.\n", tier, reqMem);
        } else {
            add_tier_recursive(tier, &solvable);
        }
    }
    print_tier_tree_status(solvable);
    return solvable;
}

/*********************** End File-based Tree Builder *************************/

/**************************** Tree Utilities *******************************/

/**
 * @brief Initilizes and builds the entire tier tree, returning a
 * list of immediately solvable tiers. Does nothing and returns NULL
 * if tier tree has already been initialized.
 */
TierTreeEntryList *tier_tree_init(uint8_t nPiecesMax, uint64_t nthread) {
    if (tree) return NULL;
    nbuckets = DEFAULT_BUCKETS[nPiecesMax];
    tree = safe_calloc(nbuckets, sizeof(tier_tree_entry_t*));
    return build_tree_multithread(nPiecesMax, nthread);
}

TierTreeEntryList *tier_tree_init_from_file(const char *filename, uint64_t mem) {
    if (tree) return NULL;
    nbuckets = DEFAULT_BUCKETS[6]; // Estimated upper bound.
    tree = safe_calloc(nbuckets, sizeof(tier_tree_entry_t*));
    return build_tree_from_file(filename, mem);
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
    free(tree); tree = NULL;
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
 * TIER. Returns NULL if the given TIER is not found.
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
static void tier_tree_add(const char *tier, uint8_t nChildren,
                          pthread_mutex_t *treeLock) {
    uint64_t slot = strhash(tier) % nbuckets;
    tier_tree_entry_t *e = safe_malloc(sizeof(tier_tree_entry_t));
    memcpy(e->tier, tier, TIER_STR_LENGTH_MAX);
    e->numUnsolvedChildren = nChildren;
    if (treeLock) pthread_mutex_lock(treeLock);
    e->next = tree[slot];
    tree[slot] = e;
    ++nelements;
    if (treeLock) pthread_mutex_unlock(treeLock);
}

static void solvable_list_add(const char *tier, TierTreeEntryList **solvable, pthread_mutex_t *solvableLock) {
    /* Do not add if the given tier has already been added. */
    for (tier_tree_entry_t *walker = *solvable; walker; walker = walker->next) {
        if (!strncmp(tier, walker->tier, TIER_STR_LENGTH_MAX)) return;
    }

    tier_tree_entry_t *e = safe_malloc(sizeof(tier_tree_entry_t));
    memcpy(e->tier, tier, TIER_STR_LENGTH_MAX);
    e->numUnsolvedChildren = 0;
    if (solvableLock) pthread_mutex_lock(solvableLock);
    e->next = *solvable;
    *solvable = e;
    if (solvableLock) pthread_mutex_unlock(solvableLock);
}

static void print_tier_tree_status(TierTreeEntryList *solvable) {
    printf("total number of buckets: %"PRIu64"\n", nbuckets);
    printf("total number of elements: %"PRIu64"\n", nelements);
    printf("solvable tiers: ");
    for (TierTreeEntryList *walker = solvable; walker; walker = walker->next) {
        printf("[%s] ", walker->tier);
    }
    printf("\n");
}

/***************************** End Helper Functions ******************************/
