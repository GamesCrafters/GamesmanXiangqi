CC = gcc
CFLAGS = -Wall -pthread -g
OBJ_DIR = obj
TEST_DIR = tests
TEST_OBJ_DIR = $(TEST_DIR)/$(OBJ_DIR)
BIN_DIR = bin

DEPS = common.h db.h frontier.h game.h misc.h solver.h tier.h tiersolver.h tiertree.h

_TEST_DEPS = db_test.h game_test.h tests.h tier_test.h tiersolver_test.h
TEST_DEPS = $(patsubst %, $(TEST_DIR)/%, $(_TEST_DEPS))

_CORE_OBJ = common.o db.o frontier.o game.o misc.o solver.o tier.o tiersolver.o tiertree.o
CORE_OBJ = $(patsubst %, $(OBJ_DIR)/%, $(_CORE_OBJ))

# Main solver.
SOLVER_OBJ = $(OBJ_DIR)/main.o

_TEST_OBJ = db_test.o game_test.o tier_test.o tiersolver_test.o
TEST_OBJ = $(patsubst %, $(TEST_OBJ_DIR)/%, $(_TEST_OBJ))

# Module that querys the DB forever from STDIN.
QUERY_OBJ = $(TEST_OBJ_DIR)/query.o

# Module that runs an experimental program.
EXPERIMENT_OBJ = $(TEST_OBJ_DIR)/experiment.o

# Module that runs all tests.
TESTS_OBJ = $(TEST_OBJ_DIR)/tests.o

all: rule_solver rule_other

rule_solver: $(BIN_DIR)/solve

rule_other: $(BIN_DIR)/query $(BIN_DIR)/experiment $(BIN_DIR)/tests

.PHONY: clean

clean:
	rm -f $(OBJ_DIR)/*.o $(TEST_OBJ_DIR)/*.o $(BIN_DIR)/* *~ core

$(BIN_DIR)/solve: $(CORE_OBJ) $(SOLVER_OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

$(BIN_DIR)/query: $(CORE_OBJ) $(TEST_OBJ) $(QUERY_OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

$(BIN_DIR)/tests: $(CORE_OBJ) $(TEST_OBJ) $(TESTS_OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

$(BIN_DIR)/experiment: $(CORE_OBJ) $(TEST_OBJ) $(EXPERIMENT_OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

# TODO: fine-grained headers
$(OBJ_DIR)/%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

$(TEST_OBJ_DIR)/%.o: $(TEST_DIR)/%.c $(DEPS) $(TEST_DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)
