#include "tree.h"
#include "indexer.h"
#include "parse.h"

//from regret sums
void calc_strategy(float* regret_sum, float* strategy, int num_actions, int num_buckets) {
	for (int b = 0; b < num_buckets; b++) {
		float normalizing_sum = 0.0f;

		//sum of all pos regrets
		for (int a = 0; a < num_actions; a++) {
			int idx = (a * num_buckets) + b;
			strategy[idx] = regret_sum[idx] > 0.0f ? regret_sum[idx] : 0.0f;
			normalizing_sum += strategy[idx];
		}

		//normalize
		for (int a = 0; a < num_actions; a++) {
			int idx = (a * num_buckets) + b;
			if (normalizing_sum > 0.0f)
				strategy[idx] /= normalizing_sum;
			else
				strategy[idx] = 1.0f/(float)num_actions;
		}
	}
}

void walk_tree(PublicNode* node, GameState state, IsoMap* map, int num_buckets, float* p1_reach, float* p2_reach, float* out_util) {
	if (node->type == NODE_TERMINAL) {
		evaluate_showdown(state, map, num_buckets, p1_reach, p2_reach, out_util);
		return;
	}

	if (node->type == NODE_CHANCE) {
		memset(out_util, 0, num_buckets * sizeof(float));
		float* child_util = (float*) malloc(num_buckets * sizeof(float));

		for (int i = 0; i < node->num_children; i++) {
			GameState next_state = apply_deal(state, node->dealt_cards[i]);
			walk_tree(node->children[i], next_state, map, num_buckets, p1_reach, p2_reach, child_util);
			float p_card = node->chance_weights[i] / 45.0f; //approx unseen cards
			for (int b = 0; b < num_buckets; b++)
				out_util[b] += child_util[b] * p_card;
		}
		free(child_util);
		return;
	}

	//action node
	int active = node->active_player;
	int num_actions = node->num_children;

	int legal_actions[8];
	generate_bet_sizes(&state, legal_actions);

	float* strategy = (float*)malloc(num_actions * num_buckets * sizeof(float));
	float* action_utils = (float*)malloc(num_actions * num_buckets * sizeof(float));

	//calculate strat based on accumulated regrests
	calc_strategy(node->regret_sum, strategy, num_actions, num_buckets);
	memset(out_util, 0, num_buckets * sizeof(float));

	//walk each action branch
	for (int a = 0; a < num_actions; a++) {
		float* next_p1_reach = (float*)malloc(num_buckets*sizeof(float));
		float* next_p2_reach = (float*)malloc(num_buckets*sizeof(float));
		memcpy(next_p1_reach, p1_reach, num_buckets * sizeof(float));
		memcpy(next_p2_reach, p2_reach, num_buckets * sizeof(float));

		//update reach prob for each player
		for (int b = 0; b < num_buckets; b++) {
			int idx = (a * num_buckets) + b;
			if (active == 0)
				next_p1_reach[b] *= strategy[idx];
			else
				next_p2_reach[b] *= strategy[idx];
		}
		
		GameState next_state = apply_bet(state, legal_actions[a]);

		float* child_util = &action_utils[a * num_buckets];
		walk_tree(node->children[a], next_state, map, num_buckets, next_p1_reach, next_p2_reach, child_util);

		//returned utility perspective of child nodes active palye,r we must swap it
		for (int b = 0; b < num_buckets; b++) {
			child_util[b] = -child_util[b];
			out_util[b]  += strategy[(a * num_buckets) + b] * child_util[b];
		}

		free(next_p1_reach);
		free(next_p2_reach);
	}

	//regret updates
	for (int a = 0; a < num_actions; a++) {
		for (int b = 0; b < num_buckets; b++) {
			int idx = (a * num_buckets) + b;
			//cfr "how much ev did i get for this vs average ev"
			float regret = action_utils[(a * num_buckets) + b] - out_util[b];
			//weight the regret by prob that the opponent will reach this node
			float opp_reach = (active == 0) ? p2_reach[b] : p1_reach[b];

			node->regret_sum[idx] += regret * opp_reach;
			//add to strategy sum
			node->strategy_sum[idx] += strategy[idx] * ((active == 0) ? p1_reach[b] : p2_reach[b]);

		}
	}

	free(strategy);
	free(action_utils);
}

void do_cfr_iteration(PublicNode* root, GameState initial_state, IsoMap* map, int num_buckets) {
	float* p1_reach  = (float*)malloc(num_buckets * sizeof(float));
	float* p2_reach  = (float*)malloc(num_buckets * sizeof(float));
	float* root_util = (float*)malloc(num_buckets * sizeof(float));

	for (int i = 0; i < num_buckets; i++) {
		p1_reach[i] = 1.0f;
		p2_reach[i] = 1.0f;
		root_util[i] = 0.0f;
	}

	walk_tree(root, initial_state, map, num_buckets, p1_reach, p2_reach, root_util);

	free(p1_reach);
	free(p2_reach);
	free(root_util);
}
