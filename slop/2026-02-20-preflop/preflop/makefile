CC = gcc
CFLAGS = -Wall -Wextra -O3 -march=native -I include -pthread
SRC_DIR = src
OBJ_DIR = obj
OUT_DIR = output
UNAME_S := $(shell uname -s)

OPENMP_CFLAGS = -fopenmp
OPENMP_LDFLAGS = 

TARGET = $(OUT_DIR)/preflop_solver
TEST_TARGET = $(OUT_DIR)/test_preflop

# Library sources (everything except main.c and test_preflop.c)
LIB_SRCS = $(filter-out $(SRC_DIR)/main.c $(SRC_DIR)/test_preflop.c, $(wildcard $(SRC_DIR)/*.c))
LIB_OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(LIB_SRCS))

MAIN_OBJ = $(OBJ_DIR)/main.o
TEST_OBJ = $(OBJ_DIR)/test_preflop.o

all: dirs $(TARGET)

test: dirs $(TEST_TARGET)
	./$(TEST_TARGET)

dirs:
	@mkdir -p $(OBJ_DIR) $(OUT_DIR)

$(TARGET): $(LIB_OBJS) $(MAIN_OBJ)
	$(CC) $(CFLAGS) $(OPENMP_CFLAGS) -o $@ $^ -lncurses -pthread $(OPENMP_LDFLAGS)

$(TEST_TARGET): $(LIB_OBJS) $(TEST_OBJ)
	$(CC) $(CFLAGS) $(OPENMP_CFLAGS) -o $@ $^ -pthread $(OPENMP_LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(OPENMP_CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(OUT_DIR)

.PHONY: all dirs clean test
