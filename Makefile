CC      ?= cc
CFLAGS  ?= -std=c17 -Wall -Wextra -Werror -pedantic -O2
LDLIBS  ?= -lm
INCLUDE  = -Iinclude

BUILD    = build
LIB_SRC  = src/token_bucket.c
CLI_SRC  = src/main.c
TEST_SRC = tests/test_token_bucket.c

.PHONY: all test asan valgrind clean

all: $(BUILD)/ratelimit

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/ratelimit: $(LIB_SRC) $(CLI_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDE) $(LIB_SRC) $(CLI_SRC) -o $@ $(LDLIBS)

$(BUILD)/tests: $(LIB_SRC) $(TEST_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDE) $(LIB_SRC) $(TEST_SRC) -o $@ $(LDLIBS)

test: $(BUILD)/tests
	$(BUILD)/tests

asan: | $(BUILD)
	$(CC) $(CFLAGS) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
		$(INCLUDE) $(LIB_SRC) $(TEST_SRC) -o $(BUILD)/tests-asan $(LDLIBS)
	$(BUILD)/tests-asan

valgrind: $(BUILD)/tests
	valgrind --error-exitcode=1 --leak-check=full --errors-for-leak-kinds=all \
		$(BUILD)/tests

clean:
	rm -rf $(BUILD)
