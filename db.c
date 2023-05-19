#include "db.h"
#include "mgz.h"
#include "misc.h"
#include "tier.h"
#include <assert.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#define ENOUGH_SPACE 100 // For file names.
#define GZ_EXT ".gz"
#define GZ_MAX_LEVEL 9
#define MGZ_BLOCK_SIZE (1 << 20) // 1 MiB.

/* Important note: gzread returns the number of bytes read, whereas
   fread returns the number of items read. */

/* Helper functions. */
static void get_rem(const char *tier, char *rem);
static char *get_dirname(const char *tier);
static char *get_tier_filename(const char *tier, bool gz);
static char *get_lookup_filename(const char *tier);
static char *get_stat_filename(const char *tier);

static FILE *fopen_tier(const char *tier, const char *modes, bool gz) {
    char *dirname = get_dirname(tier);
    char *filename = get_tier_filename(tier, gz);

    /* Create target directory. */
    mkdir(dirname, 0777);
    free(dirname);

    /* Open file from target directory. */
    FILE *fp = fopen(filename, modes);
    free(filename);
    return fp;
}

static gzFile gzopen_tier(const char *tier, const char *modes) {
    char *dirname = get_dirname(tier);
    char *filename = get_tier_filename(tier, true);

    /* Create target directory. */
    mkdir(dirname, 0777);
    free(dirname);

    /* Open file from target directory. */
    gzFile file = gzopen(filename, modes);
    free(filename);
    return file;
}

static FILE *fopen_lookup(const char *tier, const char *modes) {
    char *dirname = get_dirname(tier);
    char *lookupFileName = get_lookup_filename(tier);

    /* Create target directory. */
    mkdir(dirname, 0777);
    free(dirname);

    /* Open file from target directory. */
    FILE *fp = fopen(lookupFileName, modes);
    free(lookupFileName);
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
    gzFile f = gzopen_tier(tier, "rb");
    if (f == Z_NULL) {
        printf("db_get_value: failed to open tier %s\n", tier);
        exit(1);
    }
    uint16_t res;
    gzseek(f, hash*sizeof(uint16_t), SEEK_SET);
    if (gzread(f, &res, sizeof(res)) != sizeof(res)) {
        printf("db_get_value: error reading position %"PRIu64
               " from tier %s.\n", hash, tier);
        exit(1);
    }
    gzclose(f);
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
    char *filename = get_tier_filename(tier, true);
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
        /* Check again for raw bytes. */
        free(filename);
        filename = get_tier_filename(tier, false);
        fp = fopen(filename, "rb");
        if (!fp) {
            /* No raw bytes found. */
            ret = DB_TIER_MISSING;
            goto _bailout;
        }
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

static bool tier_file_is_valid(const char *tier, const uint16_t *values,
                               uint64_t tierSize) {
    int db_tier_status = db_check_tier(tier);
    if (db_tier_status == DB_TIER_MISSING) return false;

    /* Check if tier file already exists and contains the same data. */
    uint16_t *existingValues = db_load_tier(tier, tierSize);
    if (!existingValues) return false;
    if (memcmp(existingValues, values, tierSize)) {
        printf("tier_file_is_valid: (fatal) new solver result does not match "
               "old database in tier %s.\n", tier);
        exit(1);
    }
    free(existingValues);
    printf("tier_file_is_valid: test passed.\n");
    return true;
}

static void db_save_tier_write_lookup_table(const char *tier,
                                            uint64_t *outBlockSizes,
                                            uint64_t nOutBlocks) {
    uint64_t t1 = 0, t2 = 0;
    for (uint64_t i = 0; i < nOutBlocks; ++i) {
        t2 = t1 + outBlockSizes[i];
        outBlockSizes[i] = t1;
        t1 = t2;
    }
    FILE *fp = fopen_lookup(tier, "wb");
    fwrite(&nOutBlocks, sizeof(uint64_t), 1, fp);
    fwrite(outBlockSizes, sizeof(uint64_t), nOutBlocks, fp);
    fclose(fp);
    free(outBlockSizes);
}

void db_save_tier(const char *tier, const uint16_t *values, uint64_t tierSize) {
    /* If the tier file is believed to be intact, skip saving. */
    if (tier_file_is_valid(tier, values, tierSize)) return;
    mgz_res_t mgzRes = mgz_parallel_deflate(values, tierSize * sizeof(uint16_t),
                                            GZ_MAX_LEVEL, MGZ_BLOCK_SIZE, true);
    if (mgzRes.out) {
        /* In-memory compression succesfully completed, write it to disk. */
        FILE *fp = fopen_tier(tier, "wb", true);
        fwrite(mgzRes.out, 1, mgzRes.size, fp);
        fclose(fp);
        free(mgzRes.out);

        /* Write the lookup table. */
        db_save_tier_write_lookup_table(tier, mgzRes.outBlockSizes, mgzRes.nOutBlocks);
    } else {
        /* OOM occured during compression, fall back to storing raw bytes. */
        printf("db_save_tier: mgz compression failed, storing tier %s "
               "in raw bytes\n", tier);
        FILE *fp = fopen_tier(tier, "wb", false);
        fwrite(values, sizeof(uint16_t), tierSize, fp);
        fclose(fp);
    }
}

void db_save_stat(const char *tier, const tier_solver_stat_t stat) {
    FILE *fp = fopen_stat(tier, "wb");
    fwrite(&stat, sizeof(stat), 1, fp);
    fclose(fp);
}

/* Loads values from TIER of size TIERSIZE into a malloc'ed array
   and return a pointer to the array. The user of this function
   is responsible for freeing the array. Assumes that TIER exists
   in the database.
   
   Returns a pointer to a malloc'ed array of size 2*TIERSIZE bytes
   containing the values of tier TIER if no error occurs. Returns
   NULL if malloc failed. Terminates the program if TIER does not
   exist in database. */
uint16_t *db_load_tier(const char *tier, uint64_t tierSize) {
    bool gz = true;
    gzFile gzLoadFile = Z_NULL;
    FILE *loadfile = NULL;
    uint16_t *values = (uint16_t*)malloc(tierSize * sizeof(uint16_t));
    if (!values) return NULL;

    gzLoadFile = gzopen_tier(tier, "rb");
    if (gzLoadFile == Z_NULL) {
        /* .gz file not found. Try loading the raw bytes. */
        gz = false;
        loadfile = fopen_tier(tier, "rb", false);
        if (!loadfile) {
            printf("load_values_from_disk: (fatal) failed to open tier %s\n", tier);
            exit(1);
        }
    }

    if (gz) {
        /* Load from gzip. */
        size_t loadSize = tierSize * sizeof(uint16_t);
        if (gzread(gzLoadFile, values, loadSize) != loadSize) {
            printf("load_values_from_disk: (fatal) failed to load all values from "
                "tier %s\n", tier);
            exit(1);
        }
        gzclose(gzLoadFile);
    } else {
        /* Load from raw bytes. */
        if (fread(values, sizeof(uint16_t), tierSize, loadfile) != tierSize) {
            printf("load_values_from_disk: (fatal) failed to load all values from "
                "tier %s\n", tier);
            exit(1);
        }
        fclose(loadfile);
    }
    return values;
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

static char *get_tier_filename(const char *tier, bool gz) {
    char *dirname = get_dirname(tier);
    // 21-char dirname, 1 forward slash, followed by filename.
    char *filename = (char *)safe_calloc(ENOUGH_SPACE, sizeof(char));
    strcat(filename, dirname);
    strcat(filename, "/");
    strcat(filename, tier);
    if (gz) strcat(filename, GZ_EXT);
    free(dirname);
    return filename;
}

static char *get_lookup_filename(const char *tier) {
    char *filename = get_tier_filename(tier, false);
    char *lookupFilename = (char *)safe_calloc(ENOUGH_SPACE, sizeof(char));
    strcat(lookupFilename, filename);
    strcat(lookupFilename, ".lookup");
    free(filename);
    return lookupFilename;
}

static char *get_stat_filename(const char *tier) {
    char *filename = get_tier_filename(tier, false);
    char *statFilename = (char *)safe_calloc(ENOUGH_SPACE, sizeof(char));
    strcat(statFilename, filename);
    strcat(statFilename, ".stat");
    free(filename);
    return statFilename;
}
