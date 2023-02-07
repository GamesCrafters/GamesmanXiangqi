#include "tiertree.h"
#include "tier.h"
#include <stdbool.h>

static const double LOAD_FACTOR = 0.75;

typedef struct TierTreeEntry {
    struct TierTreeEntry *next;
    char tier[TIER_STR_LENGTH_MAX];
    uint8_t numUnsolvedChildren;
} tier_tree_entry_t;

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

static uint64_t prev_prime(uint64_t n) {
    /* Returns the largest prime number that is smaller than
       or equal to N, unless N is less than 2, in which case
       2 is returned. */
    if (n < 2) return 2;
    while (!is_prime(n)) --n;
    return n;
}

void tier_tree_init(void) {

}

void tier_tree_destroy(void);
void tier_tree_add(const char *tier);
tier_tree_entry_t *tier_tree_find(const char *tier);
void tier_tree_remove(const char *tier);

