CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -pedantic -Iinclude -D_POSIX_C_SOURCE=200809L
LDFLAGS ?= -lm

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
BIN_DIR = bin
TEST_DIR = tests

CORE_SRCS = $(SRC_DIR)/debounce_irq.c \
            $(SRC_DIR)/gpio_auditor.c \
            $(SRC_DIR)/exporters.c \
            $(SRC_DIR)/json_parser.c

CORE_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(CORE_SRCS))

MAIN_SRC = $(SRC_DIR)/main.c
MAIN_OBJ = $(BUILD_DIR)/main.o
CLI_BIN = $(BIN_DIR)/lh-gpio-iso

TEST_SRC = $(TEST_DIR)/test_runner.c
TEST_BIN = $(TEST_DIR)/test_runner

.PHONY: all test clean

all: $(CLI_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(CLI_BIN): $(CORE_OBJS) $(MAIN_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_BIN): $(CORE_OBJS) $(TEST_SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: $(TEST_BIN)
	$(TEST_BIN)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) $(TEST_BIN)
