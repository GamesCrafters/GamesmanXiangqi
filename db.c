#include "db.h"
#include <stdlib.h>
#include <stdio.h>

uint16_t db_get_value(const char *tier, uint64_t hash) {
    FILE *f = fopen(tier, "rb");
    if (!f) exit(1);
    uint16_t res;
    fseek(f, hash*sizeof(uint16_t), SEEK_SET);
    fread(&res, sizeof(res), 1, f);
    fclose(f);
    return res;
}
