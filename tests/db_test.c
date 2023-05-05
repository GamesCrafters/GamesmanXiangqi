#include "db_test.h"
#include "../game.h"
#include "../db.h"
#include "../common.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void db_test_print_optimal_play(const char *tier, uint64_t hash) {
    board_t board;
    uint16_t val = db_get_value(tier, hash);
    ext_pos_array_t children;
    char currTier[TIER_STR_LENGTH_MAX];
    uint64_t currHash = hash;

    memcpy(currTier, tier, TIER_STR_LENGTH_MAX);
    game_init_board(&board);
    
    if (val == 0) {
        printf("position %"PRIu64" is INVALID in tier %s.\n", hash, tier);
        return;
    } else if (val == DRAW_VALUE) {
        printf("position %"PRIu64" in tier %s is a DRAW position.\n", hash, tier);
        return;
    }
    while (val > 1 && val < UINT16_MAX) {
        game_unhash(&board, currTier, currHash);
        printf("position %"PRIu64" in tier [%s] has value %d:\n", currHash, currTier, val);
        print_board(&board);
        clear_board(&board);
        getc(stdin);

        if (val == DRAW_VALUE) {
            printf("db_test_print_optimal_play: draw position reached from"
                   " a non-draw position in optimal play.\n");
            return;
        }
        children = game_get_children(currTier, currHash);
        if (children.size == 0) {
            printf("db_test_print_optimal_play: illegal position reached "
                   "from a legal position.\n");
            return;
        }
        /* Loop through the list of children and pick the best one. */
        int8_t bestIdx = 0;
        val = db_get_value(children.array[0].tier, children.array[0].hash);
        for (int8_t i = 1; i < children.size; ++i) {
            uint16_t thisVal = db_get_value(children.array[i].tier, children.array[i].hash);
            if (thisVal < val) {
                val = thisVal;
                bestIdx = i;
            }
        }
        memcpy(currTier, children.array[bestIdx].tier, TIER_STR_LENGTH_MAX);
        currHash = children.array[bestIdx].hash;

        free(children.array); children.array = NULL;
    }

    game_unhash(&board, currTier, currHash);
    printf("position %"PRIu64" in tier [%s] has value %d:\n", currHash, currTier, val);
    print_board(&board);
    clear_board(&board);
}

#define OK       0
#define NO_INPUT 1
#define TOO_LONG 2
static int getLine (char *prmpt, char *buff, size_t sz) {
    int ch, extra;

    // Get line with buffer overrun protection.
    if (prmpt != NULL) {
        printf ("%s", prmpt);
        fflush (stdout);
    }
    if (fgets (buff, sz, stdin) == NULL)
        return NO_INPUT;

    // If it was too long, there'll be no newline. In that case, we flush
    // to end of line so that excess doesn't affect the next call.
    if (buff[strlen(buff)-1] != '\n') {
        extra = 0;
        while (((ch = getchar()) != '\n') && (ch != EOF))
            extra = 1;
        return (extra == 1) ? TOO_LONG : OK;
    }

    // Otherwise remove newline and give string back to caller.
    buff[strlen(buff)-1] = '\0';
    return OK;
}

void db_test_query_forever(void) {
    board_t board;
    game_init_board(&board);

    char tier[25];
    char buff[25];
    uint64_t hash;
    while (true) {
        getLine("enter tier, or leave emtpy to use previous tier> ", buff, sizeof(buff));
        if (strlen(buff)) strncpy(tier, buff, 25);
        getLine("enter hash> ", buff, sizeof(buff));
        if (!strlen(buff)) return;
        hash = (uint64_t)atoll(buff);
        game_unhash(&board, tier, hash);
        print_board(&board);
        clear_board(&board);
        printf("[rmt(%"PRIu64") in tier %s: %d]\n", hash, tier, db_get_value(tier, hash));
        printf("game_num_child_pos(%"PRIu64"): %d\n", hash, game_num_child_pos(tier, hash, &board));

        pos_array_t parents = game_get_parents(tier, hash, tier, (tier_change_t){INVALID_IDX, 0, INVALID_IDX, 0}, &board);
        printf("parent positions in the same tier: ");
        for (int8_t i = 0; i < parents.size; ++i) {
            printf("[%"PRIu64"] ", parents.array[i]);
        }
        printf("\n");
        free(parents.array);
    }
}

/* Scans all files in the database and check for their integrity. */
void db_test_sanity(void) {
// DIR *dir;
// struct dirent *ent;
// if ((dir = opendir ("c:\\src\\")) != NULL) {
//   /* print all the files and directories within directory */
//   while ((ent = readdir (dir)) != NULL) {
//     printf ("%s\n", ent->d_name);
//   }
//   closedir (dir);
// } else {
//   /* could not open directory */
//   perror ("");
//   return EXIT_FAILURE;
// }
}
