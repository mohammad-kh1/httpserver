# --- Build Configuration ---
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -pthread
LDFLAGS =
TARGET = build/httpserver
SRC_DIR = src
BUILD_DIR = build
SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(SRC:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

.PHONY: all build clean run

# Default target
all: build

# Create build directory if it doesn't exist
$(BUILD_DIR):
	@mkdir -p $@

# Link Step: Creates the final executable
$(TARGET): $(OBJ) | $(BUILD_DIR)
	@echo "Linking $@"
	$(CC) $(LDFLAGS) $^ -o $@

# Compile Step: Compiles source files into object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build artifacts
clean:
	@echo "Cleaning up build directory..."
	@rm -rf $(BUILD_DIR)

# Run the server on port 8080 (assumes it's already built)
run: $(TARGET)
	@echo "Starting server on port 8080. Press Ctrl+C to stop."
	./$(TARGET) 8080
