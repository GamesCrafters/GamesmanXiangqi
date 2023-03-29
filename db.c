#include "db.h"
#include "tier.h"
#include "misc.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

#define ENOUGH_SPACE 100

/* Helper functions. */
static void get_rem(const char *tier, char *rem);
static char *get_dirname(const char *tier);
static char *get_tier_filename(const char *tier);
static char *get_stat_filename(const char *tier);

uint16_t db_get_value(const char *tier, uint64_t hash) {
    FILE *f = db_fopen_tier(tier, "rb");
    if (!f) exit(1);
    uint16_t res;
    fseek(f, hash*sizeof(uint16_t), SEEK_SET);
    fread(&res, sizeof(res), 1, f);
    fclose(f);
    return res;
}

FILE *db_fopen_tier(const char *tier, const char *modes) {
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

FILE *db_fopen_stat(const char *tier, const char *modes) {
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

int db_check_tier(const char *tier) {
    char *dirname = get_dirname(tier);
    char *filename = get_tier_filename(tier);
    char *statFilename = get_stat_filename(tier);
    int ret = DB_TIER_MISSING;

    /* Check target directory. */
    DIR *dp = opendir(dirname);
    if (!dp) {
        ret = DB_TIER_MISSING;
        goto _bailout;
    }
    closedir(dp);
    /* Check tier file in target directory. */
    FILE *fp = fopen(filename, "rb");
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
    fread(&st, sizeof(st), 1, fp);
    fclose(fp);
    return st;
}

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
