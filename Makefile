CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude
TEST_CFLAGS = $(CFLAGS) -DSQLPARSER_BENCHMARK_NO_MAIN

BUILD_DIR = build
BIN_DIR = $(BUILD_DIR)/bin

COMMON_SOURCES = \
	src/common/util.c \
	src/storage/schema.c \
	src/storage/storage.c \
	src/sql/ast.c \
	src/sql/lexer.c \
	src/sql/parser.c \
	src/execution/executor.c \
	src/index/bptree.c \
	src/index/table_index.c

APP_SOURCES = src/app/main.c $(COMMON_SOURCES)
TEST_SOURCES = tests/test_runner.c src/benchmark/benchmark_main.c $(COMMON_SOURCES)
BENCHMARK_SOURCES = src/benchmark/benchmark_main.c $(COMMON_SOURCES)

APP_BIN = $(BIN_DIR)/sqlparser
TEST_BIN = $(BIN_DIR)/test_runner
BENCHMARK_BIN = $(BIN_DIR)/benchmark_runner

.PHONY: all test benchmark clean

all: $(APP_BIN)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(APP_BIN): $(APP_SOURCES) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(APP_SOURCES)

$(TEST_BIN): $(TEST_SOURCES) | $(BIN_DIR)
	$(CC) $(TEST_CFLAGS) -o $@ $(TEST_SOURCES)

$(BENCHMARK_BIN): $(BENCHMARK_SOURCES) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(BENCHMARK_SOURCES)

test: $(TEST_BIN)
	./$(TEST_BIN)

benchmark: $(BENCHMARK_BIN)

clean:
	rm -rf $(BUILD_DIR)
