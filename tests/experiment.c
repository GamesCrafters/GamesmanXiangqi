#include "db_test.h"
#include "../common.h"

int main(int argc, char *argv[]) {
    make_triangle();
    db_test_print_optimal_play("000000000012__", 26156611ULL);
    return 0;
}
