# Makefile for native testing of OpenPrintTag library
#
# Usage:
#   make          - Build and run tests
#   make test     - Build and run tests
#   make clean    - Clean build artifacts

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -O0
LDFLAGS = -lm

# Directories
LIB_DIR = ../lib/openprinttag
BUILD_DIR = build

# Source files
LIB_SOURCES = $(LIB_DIR)/openprinttag_lib.c $(LIB_DIR)/cbor_native.c
TEST_SOURCES = test_openprinttag.c

# Include paths
INCLUDES = -I$(LIB_DIR)

# Output
TEST_BINARY = $(BUILD_DIR)/test_openprinttag

.PHONY: all test clean

all: test

test: $(TEST_BINARY)
	@echo "Running tests..."
	@./$(TEST_BINARY)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TEST_BINARY): $(BUILD_DIR) $(LIB_SOURCES) $(TEST_SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(LIB_SOURCES) $(TEST_SOURCES) $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
