#include "db_test.h"
#include "../common.h"
#include "tiersolver_test.h"

int main(int argc, char *argv[]) {
    make_triangle();
    tiersolver_test_solve_single_tier("221001000000__0");
    return 0;
}
