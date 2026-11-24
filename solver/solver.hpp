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
#define PASS  1
#define CALL  1
#define CHECK 1
#define RAISE 2
#define RAISE_AMT 2.5

#define ACTION   0
#define CHANCE   1
#define TERMINAL 2

#define MAX_RAISES 2

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
	int street_start_commit = 0;
	int last_raise = 0;
};

#include "chance.hpp"
#include <map>

struct Node {
    std::vector<Node> children;
    std::vector<float> regret_sum, strategy_sum;
};

class Solver {
public:
    Solver(std::array<turbofire::Card, 3> flop, std::vector<float> hero,
           std::vector<float> villain, bool share);
    std::vector<float> train(GameState state, std::optional<turbofire::Card> turn, int iterations);
    std::size_t action_nodes = 0;
    std::size_t strategy_bytes = 0;

private:
    using Reach = std::array<std::vector<float>, 2>;
    struct DealGroup {
        turbofire::Card card;
        std::vector<turbofire::SuitPermutation> mappings;
    };
    struct BoardHands {
        std::vector<int> combos;
        // Indices into combos for hands with positive starting weight.
        std::array<std::vector<int>, 2> in_range;
    };
    std::map<std::uint64_t, BoardHands> board_hands;
    const BoardHands& hands_on_board(std::uint64_t board);
    Reach ranges;
    Node root;
    std::optional<std::pair<std::uint64_t, int>> root_layout;
    std::map<int, std::vector<DealGroup>> groups;
    std::map<turbofire::SuitPermutation, std::array<int,1326>> permutations;
    std::array<std::uint64_t,1326> masks;
    std::array<std::array<int,2>,1326> cards;
    std::map<std::uint64_t, std::vector<std::pair<int,int>>> rankings;

    std::vector<float> solve(GameState state, Node& node, int traverser,
                             const Reach& reach, std::optional<turbofire::Card> turn);
    std::vector<float> chance(GameState state, Node& node, int traverser,
                              const Reach& reach, std::optional<turbofire::Card> turn);
    std::vector<float> terminal(GameState state, int traverser, const std::vector<float>& opponent);
    static GameState next_state(GameState prev, int action);
    static std::vector<int> get_legal_actions(const GameState& state);
    static std::vector<float> get_strategy(int legal_actions, int num_combos, const std::vector<float>& regret);
};
