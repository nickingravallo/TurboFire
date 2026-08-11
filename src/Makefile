CC = gcc
CXX = g++
CFLAGS_BASE = -Wall -Wextra -O3 -march=native
CXXFLAGS = -O3 -std=c++17 -I./omp

# Flop-only (soft-label / LLM data). Turn solves are multi-GB; keep separate.
FLOP_CFLAGS = $(CFLAGS_BASE) -DMAX_STREET=0 -DWALK_TREE=1
# Flop + turn betting, no river. walk_tree off by default (dump is huge).
TURN_CFLAGS = $(CFLAGS_BASE) -DMAX_STREET=1 -DWALK_TREE=0

SHARED_C_OBJS = parse.o dcfr.o ranges.o
CXX_OBJS = evaluator.o omp/HandEvaluator.o

.PHONY: all turn clean

all: turbofire

turn: turbofire_turn

turbofire: main_flop.o tree_flop.o walk_tree.o $(SHARED_C_OBJS) $(CXX_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

turbofire_turn: main_turn.o tree_turn.o $(SHARED_C_OBJS) $(CXX_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

main_flop.o: main.c config.h
	$(CC) $(FLOP_CFLAGS) -c $< -o $@

tree_flop.o: tree.c config.h
	$(CC) $(FLOP_CFLAGS) -c $< -o $@

walk_tree.o: walk_tree.c
	$(CC) $(FLOP_CFLAGS) -c $< -o $@

main_turn.o: main.c config.h
	$(CC) $(TURN_CFLAGS) -c $< -o $@

tree_turn.o: tree.c config.h
	$(CC) $(TURN_CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS_BASE) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o omp/*.o turbofire turbofire_turn
