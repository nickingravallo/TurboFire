CC = gcc
CFLAGS = -Wall -Wextra -O2 -I src
SRC_DIR = src
OBJ_DIR = obj
OUT_DIR = output

# Test Configuration
TEST_DIR = test
TEST_OUT_DIR = $(OUT_DIR)/tests

# MCCFR Configuration
MCCFR_DIR = mccfr
MCCFR_OUT_DIR = $(OUT_DIR)/mccfr

# Source & Object definitions (turbofire = solver TUI, requires ncurses)
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# Main Target: TurboFire solver TUI
TARGET = $(OUT_DIR)/turbofire
TURBOFIRE_OBJS = $(OBJS)

# Test Sources & Binaries
TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c, $(TEST_OUT_DIR)/%, $(TEST_SRCS))

# MCCFR Sources & Binaries
MCCFR_SRCS = $(wildcard $(MCCFR_DIR)/*.c)
MCCFR_BINS = $(patsubst $(MCCFR_DIR)/%.c, $(MCCFR_OUT_DIR)/%, $(MCCFR_SRCS))

# Library Objects: All objects EXCEPT the main program entry point
# We filter out turbofire.o so we can link tests/mccfr against ranks.o without double main() errors.
MAIN_OBJ = $(OBJ_DIR)/turbofire.o
LIB_OBJS = $(filter-out $(MAIN_OBJ), $(OBJS))

# --- TARGETS ---

all: dirs $(TARGET) tests mccfr

dirs:
	@mkdir -p $(OBJ_DIR) $(OUT_DIR) $(TEST_OUT_DIR) $(MCCFR_OUT_DIR)

# Link the main executable (solver TUI, requires ncurses)
$(TARGET): $(TURBOFIRE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lncurses

# Compile source files to objects
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# --- TEST RULES ---

tests: $(TEST_BINS)

$(TEST_OUT_DIR)/%: $(TEST_DIR)/%.c $(LIB_OBJS)
	$(CC) $(CFLAGS) $< $(LIB_OBJS) -o $@

# --- MCCFR RULES ---

mccfr: dirs $(MCCFR_BINS)

# Compile mccfr files
# Links against LIB_OBJS so you can use your src/ functions (like card eval) inside mccfr/
$(MCCFR_OUT_DIR)/%: $(MCCFR_DIR)/%.c $(LIB_OBJS)
	$(CC) $(CFLAGS) $< $(LIB_OBJS) -o $@

run: all
	./$(TARGET)

clean:
	rm -rf $(OBJ_DIR) $(OUT_DIR)

.PHONY: all dirs run clean tests mccfr
