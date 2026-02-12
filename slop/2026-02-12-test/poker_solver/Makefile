# MCCFR Poker Solver Makefile

# Compiler settings
CC = gcc
CFLAGS = -Wall -Wextra -O3 -march=native -I./include
LDFLAGS = -lm

# Directories
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin
DATA_DIR = data

# Source files
SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))

# Main target (exclude generate_table from main build)
MAIN_SOURCES = $(filter-out $(SRC_DIR)/generate_table.c, $(SOURCES))
MAIN_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(MAIN_SOURCES))

# Targets
TARGET = $(BIN_DIR)/poker_solver
TABLE_GEN = $(BIN_DIR)/generate_table

# Default target
all: directories $(TARGET) $(TABLE_GEN)

# Create necessary directories
directories:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR) $(DATA_DIR)

# Main solver executable
$(TARGET): $(MAIN_OBJECTS)
	@echo "Linking $@..."
	@$(CC) $(MAIN_OBJECTS) -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

# Table generator executable
$(TABLE_GEN): $(OBJ_DIR)/generate_table.o
	@echo "Linking $@..."
	@$(CC) $< -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

# Object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# Generate hand evaluation table
table: $(TABLE_GEN)
	@echo "Generating hand evaluation table..."
	@$(TABLE_GEN) $(DATA_DIR)/hand_ranks.dat

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "Clean complete"

# Clean everything including data
cleanall: clean
	@echo "Cleaning data files..."
	@rm -rf $(DATA_DIR)
	@echo "Clean complete"

# Run the solver
run: $(TARGET) table
	@echo "Starting poker solver..."
	@./$(TARGET)

# Debug build (with symbols, no optimization)
debug: CFLAGS = -Wall -Wextra -g -O0 -I./include
debug: all

# Profile build
profile: CFLAGS = -Wall -Wextra -O2 -pg -I./include
profile: LDFLAGS += -pg
profile: all

# Check code style
check:
	@echo "Checking code style..."
	@cppcheck --enable=all --suppress=missingIncludeSystem $(SRC_DIR) 2>/dev/null || echo "cppcheck not installed, skipping"

# Help
help:
	@echo "MCCFR Poker Solver Build System"
	@echo "================================"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build solver and table generator (default)"
	@echo "  table     - Generate hand evaluation lookup table (~125MB)"
	@echo "  run       - Build and run the solver"
	@echo "  clean     - Remove build artifacts"
	@echo "  cleanall  - Remove build artifacts and data files"
	@echo "  debug     - Build with debug symbols"
	@echo "  profile   - Build with profiling enabled"
	@echo "  check     - Run static analysis"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Usage:"
	@echo "  make              # Build everything"
	@echo "  make table        # Generate evaluation table"
	@echo "  ./bin/poker_solver # Run the solver"
	@echo ""

# Phony targets
.PHONY: all directories clean cleanall run debug profile check help table

# Dependencies
$(OBJ_DIR)/main.o: $(SRC_DIR)/main.c $(INC_DIR)/cards.h $(INC_DIR)/evaluator.h $(INC_DIR)/mccfr.h
$(OBJ_DIR)/cards.o: $(SRC_DIR)/cards.c $(INC_DIR)/cards.h
$(OBJ_DIR)/evaluator.o: $(SRC_DIR)/evaluator.c $(INC_DIR)/evaluator.h $(INC_DIR)/cards.h
$(OBJ_DIR)/actions.o: $(SRC_DIR)/actions.c $(INC_DIR)/actions.h
$(OBJ_DIR)/game_state.o: $(SRC_DIR)/game_state.c $(INC_DIR)/game_state.h $(INC_DIR)/cards.h $(INC_DIR)/actions.h $(INC_DIR)/evaluator.h
$(OBJ_DIR)/strategy.o: $(SRC_DIR)/strategy.c $(INC_DIR)/strategy.h $(INC_DIR)/actions.h
$(OBJ_DIR)/mccfr.o: $(SRC_DIR)/mccfr.c $(INC_DIR)/mccfr.h $(INC_DIR)/game_state.h $(INC_DIR)/strategy.h
$(OBJ_DIR)/generate_table.o: $(SRC_DIR)/generate_table.c
