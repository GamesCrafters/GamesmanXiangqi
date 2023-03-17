#include "db_test.h"
#include "../common.h"

int main(int argc, char *argv[]) {
    make_triangle();
    db_test_print_optimal_play("000000010010__", 1111148ULL);
    return 0;
}
