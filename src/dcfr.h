#ifndef DCFR_H
#define DCFR_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "tree.h"
#include "evaluator.h"
#include "parse.h"

#define FOLD  -1
#define PASS  0
#define B10   1
#define B25   2
#define B52   3
#define B100  4
#define B123  5
#define R2x   6
#define R3x   7
#define R4x   8

#define MAX_LEGAL_ACTIONS 8

#define P1 0
#define P2 1

void train(PublicNode* root, GameState initial_state, int num_combos, float* p1_starting_range, float* p2_starting_range, int iteration);
int get_legal_actions(GameState state, int8_t* out_actions);
void calculate_strategy(float* regret_sum, float* strategy, int num_actions, int num_combos);
GameState apply_bet(GameState current_state, int action);
GameState apply_deal(GameState current_state, int card_idx);
void action_node(PublicNode* node, GameState state, int num_combos, float* p1_reach, float* p2_reach, float* expected_util);
void chance_node(PublicNode* node, GameState state, int num_combos, float* p1_reach, float* p2_reach, float* expected_util);
void terminal_node(GameState state, int num_combos, float* p1_reach, float* p2_reach, float* expected_util);
void dcfr(PublicNode* node, GameState state, int num_combos, float* p1_reach, float* p2_reach, float* expected_util);
void discount_tree(PublicNode* node, int num_combos, int t, float alpha, float beta, float gamma);

#endif // DCFR_H
