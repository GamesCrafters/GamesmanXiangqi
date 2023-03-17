#include "tests.h"
#include "../common.h"

int test_all(void) {
    game_test_sanity();
    return 0;
}

int main(int argc, char *argv[]) {
    make_triangle();
    test_all();
    return 0;
}
