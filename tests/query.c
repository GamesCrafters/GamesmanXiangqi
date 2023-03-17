#include "db_test.h"
#include "../common.h"

int main(int argc, char *argv[]) {
    make_triangle();
    db_test_query_forever();
    return 0;
}
