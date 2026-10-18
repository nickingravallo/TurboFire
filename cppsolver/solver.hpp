#pragma once

#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

#define FLOP  0
#define TURN  1
#define RIVER 2

#define P1 0
#define P2 1

#define NONE -1
#define FOLD  0
#define PASS  1 //"passive"
#define CALL  1
#define CHECK 1 
#define RAISE 2

#define ACTION   0
#define CHANCE   1
#define TERMINAL 2

#define MAX_RAISES 2

//may pack tighter in the future, for now this works well enough 
struct GameState {
	std::uint64_t board;
	int active;
	int prev_act;
	int street;
	int pot;
	int p1commit;
	int p2commit;
	int p1stack;
	int p2stack;
	int raises;
};

struct Node {
	int type;
	std::vector<Node>& children;
	int num_children; 
	std::vector<uint8_t> dealt_cards;
	//zero in non-actnodes
	std::vector<double> regret_sum;
	std::vector<double> strategy_sum;
};

class Solver {
	private:
		static inline std::vector<float> handle_fold(GameState state, int traverser, int num_combos, std::vector<float> reachp1, std::vector<float> reachp2); 
	public:
		static void train(std::vector<float> p1combos, std::vector<float> p2combos);

		static void solve(GameState state, Node node, int traverser, int num_combos, std::vector<float> reachp1, std::vector<float> reachp2);

		static std::vector<float> do_terminal(GameState state, int traverser, int num_combos, std::vector<float> reachp1, std::vector<float> reachp2); 

		static std::vector<int> get_legal_actions(const GameState& state); 
		static std::vector<float> get_strategy(int legal_actions, int num_combos, const std::vector<float>& regret);
		static int payoff(GameState state, int p1, int p2, int trav); 
};

