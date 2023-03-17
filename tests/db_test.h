#ifndef DB_TEST_H
#define DB_TEST_H
#include <stdbool.h>
#include <stdint.h>

void db_test_print_optimal_play(const char *tier, uint64_t hash);
void db_test_query_forever(void);

#endif // DB_TEST_H
