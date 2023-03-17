#include "db.h"
#include "tier.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
    char filename[10 + TIER_STR_LENGTH_MAX] = "../data/";
    strcat(filename, tier);
    return fopen(filename, modes);
}
