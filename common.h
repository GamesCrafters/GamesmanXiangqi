#ifndef COMMON_H
#define COMMON_H

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

/* At most 90 empty slots and 12 pieces for the last step of hashing. */
#define CHOOSE_ROWS 91
#define CHOOSE_COLS 13

void make_triangle(void);

#endif // COMMON_H
