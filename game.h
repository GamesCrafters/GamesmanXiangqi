#ifndef GAME_H
#define GAME_H
#include <stdbool.h>
#include <stdint.h>

#define ILLEGAL_NUM_CHILD_POS UINT8_MAX

uint8_t game_num_child_pos(const char *tier, uint64_t hash);
uint64_t *game_get_parents(const char *tier, uint64_t hash, const char *targetTier);

#endif // GAME_H
