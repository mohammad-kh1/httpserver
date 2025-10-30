# --- Build Configuration ---
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -pthread -I$(SRC_DIR)/include
LDFLAGS = -pthread -lz
TARGET = httpserver
BUILD_DIR = build
SRC_DIR = src
TEST_DIR = tests

# Auto-detect all source files and define objects
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
EXECUTABLE = $(BUILD_DIR)/$(TARGET)

# Test Configuration
TEST_SRC = $(wildcard $(TEST_DIR)/test_*.c)
TEST_OBJS = $(patsubst $(TEST_DIR)/%.c, $(BUILD_DIR)/%.test.o, $(TEST_SRC))
TEST_EXECUTABLE = $(BUILD_DIR)/test_runner

# Default target
all: $(BUILD_DIR) $(EXECUTABLE)

# Create directories
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
	mkdir -p $(SRC_DIR)/include
	mkdir -p $(TEST_DIR)

# --- Linking Server ---
$(EXECUTABLE): $(OBJS)
	@echo Linking $@
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# --- Compilation (General Rule for all .c files in src/) ---
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo Compiling $<
	$(CC) $(CFLAGS) -c $< -o $@

# --- Execution ---
run: $(EXECUTABLE)
	@echo Running $(TARGET)...
	./$(EXECUTABLE) 8080

# --- Testing ---
test: $(TEST_EXECUTABLE)
	@echo Running tests...
	./$(TEST_EXECUTABLE)

$(BUILD_DIR)/%.test.o: $(TEST_DIR)/%.c
	@echo Compiling Test $<
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_EXECUTABLE): $(TEST_OBJS) $(OBJS)
	@echo Linking Test Runner $@
	$(CC) $(TEST_OBJS) $(OBJS) -o $@ $(LDFLAGS)

# --- Clean up build files ---
clean:
	@echo Cleaning up build and test directories...
	rm -rf $(BUILD_DIR)
	rm -rf $(SRC_DIR)/*.o
	rm -rf $(TEST_DIR)/*.o

.PHONY: all run clean test
