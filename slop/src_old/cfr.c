#include <omp.h> 
#include "tree.h"
#include "indexer.h"
#include "parse.h"
#include "showdown.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

//from regret sums
void calc_strategy(float* regret_sum, float* strategy, int num_actions, int num_buckets) {
	//copy positive regrets
	#pragma omp parallel if (num_buckets > 500)
	{
		for (int a = 0; a < num_actions; a++) {
			#pragma omp for simd
			for (int b = 0; b < num_buckets; b++) {
				int idx = (a * num_buckets) + b;
				strategy[idx] = regret_sum[idx] > 0.0f ? regret_sum[idx] : 0.0f;
			}
		}
	}

	//normalize
	#pragma omp parallel for simd if (num_buckets > 500)
	for (int b = 0; b < num_buckets; b++) {
		float normalizing_sum = 0.0f;

		for (int a = 0; a < num_actions; a++)
			normalizing_sum += strategy[(a * num_buckets) + b];

		for (int a = 0; a < num_actions; a++) {
			int idx = (a * num_buckets) + b;
			if (normalizing_sum > 0.0f)
				strategy[idx] /= normalizing_sum;
			else
				strategy[idx] = 1.0f/(float)num_actions;
		}
	}
}

//calculate avg strategy for best response evaluation
void calc_average_strategy(float* strategy_sum, float* avg_strategy, int num_actions, int num_buckets) {
	#pragma omp parallel for simd if(num_buckets > 500)
	for (int b = 0; b < num_buckets; b++) {
		float sum = 0.0f;
		for (int a = 0; a < num_actions; a++)
			sum += strategy_sum[(a * num_buckets) + b];
		for (int a = 0; a < num_actions; a++) {
			int idx = (a * num_buckets) + b;
			if (sum > 0.0f)
				avg_strategy[idx] = strategy_sum[idx] / sum;
			else
				avg_strategy[idx] = 1.0f / (float)num_actions;
		}
	}
}

void walk_tree(PublicNode* node, GameState state, IsoMap* map, int num_buckets, float* p1_reach, float* p2_reach, float* out_util, uint64_t* precomputed_masks) {
	if (node->type == NODE_TERMINAL) {
		evaluate_showdown(state, map, num_buckets, p1_reach, p2_reach, out_util, precomputed_masks);
		return;
	}

	if (node->type == NODE_CHANCE) {
		memset(out_util, 0, num_buckets * sizeof(float));
		float* child_util = (float*) malloc(num_buckets * sizeof(float));
		float total_weight = 0.0f;
		for (int i = 0; i < node->num_children; i++)
			total_weight += node->chance_weights[i];

		for (int i = 0; i < node->num_children; i++) {
			GameState next_state = apply_deal(state, node->dealt_cards[i]);
			walk_tree(node->children[i], next_state, map, num_buckets, p1_reach, p2_reach, child_util, precomputed_masks);
			float p_card = (total_weight > 0.0f) ? (node->chance_weights[i] / total_weight) : 0.0f;
			#pragma omp parallel for simd if(num_buckets > 500)
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
		#pragma omp parallel for simd if(num_buckets > 500)
		for (int b = 0; b < num_buckets; b++) {
			int idx = (a * num_buckets) + b;
			if (active == 0)
				next_p1_reach[b] *= strategy[idx];
			else
				next_p2_reach[b] *= strategy[idx];
		}
		
		GameState next_state = apply_bet(state, legal_actions[a]);

		float* child_util = &action_utils[a * num_buckets];
		walk_tree(node->children[a], next_state, map, num_buckets, next_p1_reach, next_p2_reach, child_util, precomputed_masks);

		//returned utility perspective of child nodes active palye,r we must swap it
		#pragma omp parallel for simd if(num_buckets > 500)
		for (int b = 0; b < num_buckets; b++) {
			child_util[b] = -child_util[b];
			out_util[b]  += strategy[(a * num_buckets) + b] * child_util[b];
		}

		free(next_p1_reach);
		free(next_p2_reach);
	}

	//regret updates
	#pragma omp parallel if(num_buckets > 500)
	{
		for (int a = 0; a < num_actions; a++) {
			#pragma omp for simd
			for (int b = 0; b < num_buckets; b++) {
				int idx = (a * num_buckets) + b;
				//cfr "how much ev did i get for this vs average ev"
				float regret = action_utils[idx] - out_util[b];
				//weight the regret by prob that the opponent will reach this node
				float opp_reach = (active == 0) ? p2_reach[b] : p1_reach[b];
				float my_reach  = (active == 0) ? p1_reach[b] : p2_reach[b];

				node->regret_sum[idx] += regret * opp_reach;
				node->strategy_sum[idx] += strategy[idx] * my_reach;
			}
		}
	}

	free(strategy);
	free(action_utils);
}

//best response walk
void walk_br_tree(PublicNode* node, GameState state, IsoMap* map, int num_buckets, int exploiter, float* p1_reach, float* p2_reach, float* out_util, uint64_t* precomputed_masks) {
	if (node->type == NODE_TERMINAL) {
		evaluate_showdown(state, map, num_buckets, p1_reach, p2_reach, out_util, precomputed_masks);
		return;
	}

	if (node->type == NODE_CHANCE) {
		memset(out_util, 0, num_buckets * sizeof(float));
		float* child_util = (float*) malloc(num_buckets * sizeof(float));
		float total_weight = 0.0f;
		for (int i = 0; i < node->num_children; i++)
			total_weight += node->chance_weights[i];

		for (int i = 0; i < node->num_children; i++) {
			GameState next_state = apply_deal(state, node->dealt_cards[i]);
			walk_br_tree(node->children[i], next_state, map, num_buckets, exploiter, p1_reach, p2_reach, child_util, precomputed_masks);
			float p_card = (total_weight > 0.0f) ? (node->chance_weights[i] / total_weight) : 0.0f;
			#pragma omp parallel for simd if(num_buckets > 500)
			for (int b = 0; b < num_buckets; b++)
				out_util[b] += child_util[b] * p_card;
		}
		free(child_util);
		return;
	}

	int active = node->active_player;
	int num_actions = node->num_children;
	int legal_actions[8];
	generate_bet_sizes(&state, legal_actions);

	float* action_utils = (float*)malloc(num_actions*num_buckets*sizeof(float));

	if (active == exploiter) {
		//we take max ev for each bucket
		#pragma omp parallel for simd if(num_buckets > 500)
		for (int b = 0; b < num_buckets; b++)
			out_util[b] = -99999999.0f;

		for (int a = 0; a < num_actions; a++) {
			GameState next_state = apply_bet(state, legal_actions[a]);
			float* child_util = &action_utils[a * num_buckets];

			walk_br_tree(node->children[a], next_state, map, num_buckets, exploiter, p1_reach, p2_reach, child_util, precomputed_masks);

			#pragma omp parallel for simd if(num_buckets > 500)
			for (int b = 0; b < num_buckets; b++) {
				float val = -child_util[b];
				if (val > out_util[b])
					out_util[b] = val;
			}
		}
	}
	else {
		float* avg_strategy = (float*)malloc(num_actions * num_buckets * sizeof(float));
		calc_average_strategy(node->strategy_sum, avg_strategy, num_actions, num_buckets);
		memset(out_util, 0, num_buckets * sizeof(float));

		for (int a = 0; a < num_actions; a++) {
			float* next_p1_reach = (float*)malloc(num_buckets * sizeof(float));
			float* next_p2_reach = (float*)malloc(num_buckets * sizeof(float));
			memcpy(next_p1_reach, p1_reach, num_buckets * sizeof(float));
			memcpy(next_p2_reach, p2_reach, num_buckets * sizeof(float));

			#pragma omp parallel for simd if(num_buckets > 500)
			for (int b = 0; b < num_buckets; b++) {
				int idx = (a * num_buckets) + b;
				if (active == 0)
					next_p1_reach[b] *= avg_strategy[idx];
				else
					next_p2_reach[b] *= avg_strategy[idx];
			}

			GameState next_state = apply_bet(state, legal_actions[a]);
			float* child_util = &action_utils[a * num_buckets];

			walk_br_tree(node->children[a], next_state, map, num_buckets, exploiter, next_p1_reach, next_p2_reach, child_util, precomputed_masks);

			#pragma omp parallel for simd if(num_buckets > 500)
			for (int b = 0; b < num_buckets; b++) {
				child_util[b] = -child_util[b];
				out_util[b] += avg_strategy[(a * num_buckets) + b] * child_util[b];
			}

			free(next_p1_reach);
			free(next_p2_reach);
		}
		free(avg_strategy);
	}
	free(action_utils);
}

float calc_exploitability(PublicNode* root, GameState initial_state, IsoMap* map, int num_buckets, float* p1_starting_range, float* p2_starting_range) {
	float br_total_ev = 0.0f;
	uint64_t* precomputed_masks = (uint64_t*)malloc(num_buckets * sizeof(uint64_t));
	for (int i = 0; i < num_buckets; i++)
		precomputed_masks[i] = get_mask_for_bucket(map,i);

	for (int exploiter = 0; exploiter < 2; exploiter++) {
		float* p1_reach  = (float*)malloc(num_buckets * sizeof(float));
		float* p2_reach  = (float*)malloc(num_buckets * sizeof(float));
		float* root_util = (float*)malloc(num_buckets * sizeof(float));

		for (int i = 0; i < num_buckets; i++) {
			p1_reach[i] = p1_starting_range[i];
			p2_reach[i] = p2_starting_range[i];
			root_util[i] = 0.0f;
		}

		walk_br_tree(root, initial_state, map, num_buckets, exploiter, p1_reach, p2_reach, root_util, precomputed_masks);
		float player_ev = 0.0f;
		for (int b = 0; b < num_buckets; b++) {
			if (exploiter == 0)
				player_ev += root_util[b] * p1_starting_range[b];
			else
				player_ev += root_util[b] * p2_starting_range[b];
		}

		br_total_ev += player_ev;

		free(p1_reach);
		free(p2_reach);
		free(root_util);
	}

	free(precomputed_masks);
	return br_total_ev / 2.0f;
}

void do_cfr_iteration(PublicNode* root, GameState initial_state, IsoMap* map, int num_buckets, float* p1_starting_range, float* p2_starting_range) {
	float* p1_reach  = (float*)malloc(num_buckets * sizeof(float));
	float* p2_reach  = (float*)malloc(num_buckets * sizeof(float));
	float* root_util = (float*)malloc(num_buckets * sizeof(float));
	uint64_t* precomputed_masks = (uint64_t*)malloc(num_buckets * sizeof(uint64_t));

	for (int i = 0; i < num_buckets; i++) {
		p1_reach[i] = p1_starting_range[i];
		p2_reach[i] = p2_starting_range[i];
		root_util[i] = 0.0f;
		precomputed_masks[i] = get_mask_for_bucket(map, i);
	}

	walk_tree(root, initial_state, map, num_buckets, p1_reach, p2_reach, root_util, precomputed_masks);

	free(p1_reach);
	free(p2_reach);
	free(root_util);
	free(precomputed_masks);
}

void discount_tree(PublicNode* node, int num_buckets, int t, float alpha, float beta, float gamma) {
	if (node->type == NODE_TERMINAL)
		return;

	if (node->type == NODE_ACTION) {
		float pos_disc = powf((float)t, alpha) / (powf((float)t, alpha) + 1.0f);
		float neg_disc = powf((float)t, beta)  / (powf((float)t, beta)  + 1.0f);
		float strat_disc = powf((float)t / ((float)t + 1.0f), gamma);

		int total = node->num_children * num_buckets;
		#pragma omp parallel for simd if(total > 500)
		for (int i = 0; i < total; i++) {
			if (node->regret_sum[i] > 0.0f)
				node->regret_sum[i] *= pos_disc;
			else
				node->regret_sum[i] *= neg_disc;
			node->strategy_sum[i] *= strat_disc;
		}
	}

	for (int i = 0; i < node->num_children; i++)
		discount_tree(node->children[i], num_buckets, t, alpha, beta, gamma);
}

//extract narrowed range after action of node
void extract_action_range(PublicNode* node, int num_buckets, int action_idx, float* current_reach, float* out_new_reach) {
	#pragma omp parallel for simd if(num_buckets > 500)
	for (int b = 0; b < num_buckets; b++) {
		float sum = 0.0f;

		//find total strategy sum for this hand / bucket
		for (int a = 0; a < node->num_children; a++)
			sum += node->strategy_sum[(a*num_buckets) + b];

		//calculate normalized prob of action thats taken
		float action_prob = 0.0f;
		if (sum > 0.0f)
			action_prob = node->strategy_sum[(action_idx * num_buckets) + b] / sum;

		//the new reach is old reach * strategy frequency
		out_new_reach[b] = current_reach[b] * action_prob;
	}
}
