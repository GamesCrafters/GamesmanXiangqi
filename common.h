#ifndef COMMON_H
#define COMMON_H
#include <stdint.h>

enum rem_idx {
    RED_K_IDX = -2,
    BLACK_K_IDX,
    RED_A_IDX,
    BLACK_A_IDX,
    RED_B_IDX,
    BLACK_B_IDX,
    RED_P_IDX,
    BLACK_P_IDX,
    RED_N_IDX,
    BLACK_N_IDX,
    RED_C_IDX,
    BLACK_C_IDX,
    RED_R_IDX,
    BLACK_R_IDX,
    INVALID_IDX
};

typedef struct Analysis {
    uint64_t hash_size;  /**< Number of hash values defined. */
    uint64_t win_count;  /**< Number of winning positions in total. */
    uint64_t lose_count; /**< Number of losing positions in total. */
    uint64_t draw_count; /**< Number of drawing positions in total. */

    /** Number of winning positions of each remoteness as an array. */
    uint64_t win_summary[512];
    /** Number of losing positions of each remoteness as an array. */
    uint64_t lose_summary[512];

    int largest_win_remoteness;  /**< Largest winning remoteness. */
    int largest_lose_remoteness; /**< Largest losing remoteness. */

    char largest_win_tier[32];
    char largest_lose_tier[32];
    uint64_t largest_win_pos;
    uint64_t largest_lose_pos;
} analysis_t;

/* At most 90 empty slots and 12 pieces for the last step of hashing. */
#define CHOOSE_ROWS 91
#define CHOOSE_COLS 13

/* See tiersolver.c for details. */
#define DRAW_VALUE 32768

void make_triangle(void);

extern uint64_t choose[CHOOSE_ROWS][CHOOSE_COLS];

#endif // COMMON_H
