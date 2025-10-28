# --- Build Configuration ---
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -pthread
LDFLAGS = -pthread -lz  # <-- FIX: -lz links the zlib library
TARGET = httpserver
BUILD_DIR = build
SRC_DIR = src
MAIN_SRC = $(SRC_DIR)/main.c
MAIN_OBJ = $(BUILD_DIR)/main.o
EXECUTABLE = $(BUILD_DIR)/$(TARGET)

# Default target
all: $(BUILD_DIR) $(EXECUTABLE)

# Create the build directory if it doesn't exist
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# --- Linking (The Fix is here: LDFLAGS includes -lz) ---
$(EXECUTABLE): $(MAIN_OBJ)
	@echo Linking $@
	$(CC) $(MAIN_OBJ) -o $@ $(LDFLAGS)

# --- Compilation ---
$(MAIN_OBJ): $(MAIN_SRC)
	@echo Compiling $<
	$(CC) $(CFLAGS) -c $< -o $@

# --- Execution ---
# Assumes you want to run the server on a default port (8080)
run: $(EXECUTABLE)
	@echo Running $(TARGET)...
	./$(EXECUTABLE) 8080

# --- Clean up build files ---
clean:
	@echo Cleaning up build directory...
	rm -rf $(BUILD_DIR)

.PHONY: all run clean
