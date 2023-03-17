#ifndef DB_H
#define DB_H
#include <stdint.h>
#include <stdio.h>

uint16_t db_get_value(const char *tier, uint64_t hash);
FILE *db_fopen_tier(const char *tier, const char *modes);

#endif // DB_H
