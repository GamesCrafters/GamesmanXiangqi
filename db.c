#include "db.h"
#include "misc.h"
#include "tier.h"
#include <assert.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define ENOUGH_SPACE 100 // For file names.

/* Helper functions. */
static void get_rem(const char *tier, char *rem);
static char *get_dirname(const char *tier);
static char *get_tier_filename(const char *tier);
static char *get_stat_filename(const char *tier);

static FILE *fopen_tier(const char *tier, const char *modes) {
    char *dirname = get_dirname(tier);
    char *filename = get_tier_filename(tier);

    /* Create target directory. */
    mkdir(dirname, 0777);
    free(dirname);

    /* Open file from target directory. */
    FILE *fp = fopen(filename, modes);
    free(filename);
    return fp;
}

static FILE *fopen_stat(const char *tier, const char *modes) {
    char *dirname = get_dirname(tier);
    char *statFilename = get_stat_filename(tier);

    /* Create target directory. */
    mkdir(dirname, 0777);
    free(dirname);

    /* Open file from target directory. */
    FILE *fp = fopen(statFilename, modes);
    free(statFilename);
    return fp;
}

uint16_t db_get_value(const char *tier, uint64_t hash) {
    FILE *f = fopen_tier(tier, "rb");
    if (!f) {
        printf("db_get_value: failed to open tier %s\n", tier);
        exit(1);
    }
    uint16_t res;
    fseek(f, hash*sizeof(uint16_t), SEEK_SET);
    if (fread(&res, sizeof(res), 1, f) != 1) {
        printf("db_get_value: error reading position %"PRIu64
               " from tier %s.\n", hash, tier);
        exit(1);
    }
    fclose(f);
    return res;
}

/* Returns DB_TIER_OK only if both the given tier and the
   statistics file exist in the database and are believed
   to be intact. Returns DB_TIER_MISSING if the given tier
   does not exist or is believe to be corrupted. Returns
   DB_TIER_STAT_CORRUPTED if both files exist but the
   statistics file appears to be corrupted. */
int db_check_tier(const char *tier) {
    char *dirname = get_dirname(tier);
    char *filename = get_tier_filename(tier);
    char *statFilename = get_stat_filename(tier);
    int ret = DB_TIER_MISSING;
    FILE *fp = NULL;

    /* Check target directory. */
    DIR *dp = opendir(dirname);
    if (!dp) {
        ret = DB_TIER_MISSING;
        goto _bailout;
    }
    closedir(dp);
    /* Check tier file in target directory. */
    fp = fopen(filename, "rb");
    if (!fp) {
        ret = DB_TIER_MISSING;
        goto _bailout;
    }
    fclose(fp);
    /* Check stat file in target directory. */
    fp = fopen(statFilename, "rb");
    if (!fp) {
        ret = DB_TIER_MISSING;
        goto _bailout;
    }
    fseek(fp, 0L, SEEK_END);
    if (ftell(fp) != sizeof(tier_solver_stat_t)) {
        ret = DB_TIER_STAT_CORRUPTED;
        goto _bailout;
    }
    ret = DB_TIER_OK;

_bailout:
    if (fp) fclose(fp);
    free(dirname);
    free(filename);
    free(statFilename);
    return ret;
}

tier_solver_stat_t db_load_stat(const char *tier) {
    tier_solver_stat_t st;
    char *statFilename = get_stat_filename(tier);
    FILE *fp = fopen(statFilename, "rb");
    free(statFilename);
    assert(fp);
    if (fread(&st, sizeof(st), 1, fp) != 1) {
        printf("db_load_stat: failed to read statistics file for "
               "tier %s\n", tier);
        exit(1);
    }
    fclose(fp);
    return st;
}

static bool check_tier_file(const char *tier, const uint16_t *values,
                            uint64_t tierSize) {
    int db_tier_status = db_check_tier(tier);
    if (db_tier_status == DB_TIER_MISSING) return false;

    /* Check if tier file already exists and contains the same data. */
    uint16_t *existingValues = db_load_tier(tier, tierSize);
    if (!existingValues) return false;
    if (memcmp(existingValues, values, tierSize)) {
        printf("check_tier_file: (fatal) new solver result does not match "
               "old database in tier %s.\n", tier);
        exit(1);
    }
    free(existingValues);
    printf("check_tier_file: test passed.\n");
    return true;
}

void db_save_values(const char *tier, const uint16_t *values, uint64_t tierSize) {
    if (!check_tier_file(tier, values, tierSize)) {
        /* If the tier file does not exist or is corrupted,
        (re)create it. */
        FILE *fp = fopen_tier(tier, "wb");
        fwrite(values, sizeof(uint16_t), tierSize, fp);
        fclose(fp);
    }
}

void db_save_stat(const char *tier, const tier_solver_stat_t stat) {
    FILE *fp = fopen_stat(tier, "wb");
    fwrite(&stat, sizeof(stat), 1, fp);
    fclose(fp);
}

/* Helper function definitions. */

/* Assumes enough space at rem. */
static void get_rem(const char *tier, char *rem) {
    strncpy(rem, tier, 12);
    rem[12] = '\0';
}

static char *get_dirname(const char *tier) {
    char rem[13];   // 12 pieces, 1 null terminator.
    // 8-char "../data/" prefix, 12-char rem, 1 null terminator.
    char *dirname = (char *)safe_calloc(ENOUGH_SPACE, sizeof(char)); 
    get_rem(tier, rem);
    strcat(dirname, "../data/");
    strcat(dirname, rem);
    return dirname;
}

static char *get_tier_filename(const char *tier) {
    char *dirname = get_dirname(tier);
    // 21-char dirname, 1 forward slash, followed by filename.
    char *filename = (char *)safe_calloc(ENOUGH_SPACE, sizeof(char));
    strcat(filename, dirname);
    strcat(filename, "/");
    strcat(filename, tier);
    free(dirname);
    return filename;
}

static char *get_stat_filename(const char *tier) {
    char *filename = get_tier_filename(tier);
    char *statFilename = (char *)safe_calloc(ENOUGH_SPACE, sizeof(char));
    strcat(statFilename, filename);
    strcat(statFilename, ".stat");
    free(filename);
    return statFilename;
}

/* Loads values from TIER of size TIERSIZE into a malloc'ed array
   and return a pointer to the array. The user of this function
   is responsible for freeing the array. Assumes that TIER exists
   in the database.
   
   Returns a pointer to a malloc'ed array of size 2*TIERSIZE bytes
   if no error occurs. Returns NULL if malloc failed. Terminates
   the program if TIER does not exist in database. */
uint16_t *db_load_tier(const char *tier, uint64_t tierSize) {
    FILE *loadfile;
    uint16_t *values = (uint16_t*)malloc(tierSize * sizeof(uint16_t));
    if (!values) return NULL;
    loadfile = fopen_tier(tier, "rb");
    if (!loadfile) {
        printf("load_values_from_disk: (fatal) failed to open tier %s\n", tier);
        exit(1);
    }
    if (fread(values, sizeof(uint16_t), tierSize, loadfile) != tierSize) {
        printf("load_values_from_disk: (fatal) failed to load all values from "
               "tier %s\n", tier);
        exit(1);
    }
    fclose(loadfile);
    return values;
}