CC = gcc
CXX = g++

# Apple Clang needs Homebrew libomp; GNU gcc can use plain -fopenmp.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  LIBOMP_PREFIX := $(shell brew --prefix libomp 2>/dev/null)
  ifneq ($(LIBOMP_PREFIX),)
    OPENMP_CFLAGS = -Xpreprocessor -fopenmp -I$(LIBOMP_PREFIX)/include
    OPENMP_LDFLAGS = -L$(LIBOMP_PREFIX)/lib -lomp
  else
    $(warning libomp not found; build without OpenMP. brew install libomp)
    OPENMP_CFLAGS =
    OPENMP_LDFLAGS =
  endif
else
  OPENMP_CFLAGS = -fopenmp
  OPENMP_LDFLAGS = -fopenmp
endif

CFLAGS_BASE = -Wall -Wextra -O3 -march=native $(OPENMP_CFLAGS)
CXXFLAGS = -O3 -std=c++17 -I./omp $(OPENMP_CFLAGS)
LDFLAGS = $(OPENMP_LDFLAGS)

# Flop-only (soft-label / LLM data). Turn solves are multi-GB; keep separate.
FLOP_CFLAGS = $(CFLAGS_BASE) -DMAX_STREET=0 -DWALK_TREE=1
# Flop + turn betting, no river. walk_tree off by default (dump is huge).
TURN_CFLAGS = $(CFLAGS_BASE) -DMAX_STREET=1 -DWALK_TREE=0

SHARED_C_OBJS = parse.o dcfr.o ranges.o isomorphism.o
CXX_OBJS = evaluator.o omp/HandEvaluator.o

.PHONY: all turn test clean

all: turbofire

turn: turbofire_turn

test: tests/isomorphism_test
	./tests/isomorphism_test

tests/isomorphism_test: tests/isomorphism_test.c isomorphism.c isomorphism.h
	$(CC) $(CFLAGS_BASE) tests/isomorphism_test.c isomorphism.c -o $@

turbofire: main_flop.o tree_flop.o walk_tree.o $(SHARED_C_OBJS) $(CXX_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

turbofire_turn: main_turn.o tree_turn.o $(SHARED_C_OBJS) $(CXX_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

main_flop.o: main.c config.h
	$(CC) $(FLOP_CFLAGS) -c $< -o $@

tree_flop.o: tree.c config.h isomorphism.h
	$(CC) $(FLOP_CFLAGS) -c $< -o $@

walk_tree.o: walk_tree.c
	$(CC) $(FLOP_CFLAGS) -c $< -o $@

main_turn.o: main.c config.h
	$(CC) $(TURN_CFLAGS) -c $< -o $@

tree_turn.o: tree.c config.h isomorphism.h
	$(CC) $(TURN_CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS_BASE) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o omp/*.o turbofire turbofire_turn tests/isomorphism_test
