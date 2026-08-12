#include "dcfr.h"
#include "isomorphism.h"

#include <omp.h>
#include <stdlib.h>

#define MAX_COMBOS 1326
#define MAX_ACTION_DEPTH 64

static uint64_t combo_masks[MAX_COMBOS];
static int combo_masks_ready = 0;

typedef struct {
	float* strategy;
	float* action_util;
	float* next_p1;
	float* next_p2;
	int capacity_combos;
	int capacity_actions;
} ActionFrame;

typedef struct {
	float* next_p1;
	float* next_p2;
	float* child_util;
	float* local_util;
	int capacity_combos;
} ChanceScratch;

static _Thread_local ActionFrame action_frames[MAX_ACTION_DEPTH];
static _Thread_local int action_depth = 0;
static _Thread_local ChanceScratch chance_scratch;

static ActionFrame* push_action_frame(int num_combos, int action_count) {
	ActionFrame* frame;
	int need_reach;
	int need_actions;

	if (action_depth >= MAX_ACTION_DEPTH)
		return NULL;

	frame = &action_frames[action_depth];
	need_reach = num_combos > frame->capacity_combos;
	need_actions = action_count > frame->capacity_actions || num_combos > frame->capacity_combos;

	if (need_reach) {
		free(frame->next_p1);
		free(frame->next_p2);
		frame->next_p1 = (float*)malloc((size_t)num_combos * sizeof(float));
		frame->next_p2 = (float*)malloc((size_t)num_combos * sizeof(float));
		if (!frame->next_p1 || !frame->next_p2)
			return NULL;
	}

	if (need_actions) {
		size_t action_bytes = (size_t)action_count * (size_t)num_combos * sizeof(float);

		free(frame->strategy);
		free(frame->action_util);
		frame->strategy = (float*)malloc(action_bytes);
		frame->action_util = (float*)malloc(action_bytes);
		if (!frame->strategy || !frame->action_util)
			return NULL;
		frame->capacity_actions = action_count;
	}

	if (need_reach)
		frame->capacity_combos = num_combos;

	action_depth += 1;
	return frame;
}

static void pop_action_frame(void) {
	if (action_depth > 0)
		action_depth -= 1;
}

static int ensure_chance_scratch(int num_combos) {
	ChanceScratch* s = &chance_scratch;

	if (num_combos <= s->capacity_combos)
		return 1;

	free(s->next_p1);
	free(s->next_p2);
	free(s->child_util);
	free(s->local_util);
	s->next_p1 = (float*)malloc((size_t)num_combos * sizeof(float));
	s->next_p2 = (float*)malloc((size_t)num_combos * sizeof(float));
	s->child_util = (float*)malloc((size_t)num_combos * sizeof(float));
	s->local_util = (float*)malloc((size_t)num_combos * sizeof(float));
	if (!s->next_p1 || !s->next_p2 || !s->child_util || !s->local_util)
		return 0;

	s->capacity_combos = num_combos;
	return 1;
}

static void ensure_combo_masks(int num_combos) {
	int limit = num_combos < MAX_COMBOS ? num_combos : MAX_COMBOS;

	for (int combo = combo_masks_ready; combo < limit; combo++)
		combo_masks[combo] = get_mask_for_combo(combo);

	combo_masks_ready = limit;
}

static int combo_is_dead(uint64_t combo_mask, uint64_t board_mask) {
	return (combo_mask & board_mask) != 0;
}

static int acting_commit(GameState state) {
	return state.active_player == P1 ? state.p1_commit : state.p2_commit;
}

static int opposing_commit(GameState state) {
	return state.active_player == P1 ? state.p2_commit : state.p1_commit;
}

static int acting_stack(GameState state) {
	return state.active_player == P1 ? state.p1_stack : state.p2_stack;
}

static int opposing_stack(GameState state) {
	return state.active_player == P1 ? state.p2_stack : state.p1_stack;
}

static int has_outstanding_bet(GameState state) {
	return acting_commit(state) != opposing_commit(state);
}

static int max_total_commit(GameState state) {
	int my_total = acting_commit(state) + acting_stack(state);
	int opp_total = opposing_commit(state) + opposing_stack(state);

	return my_total < opp_total ? my_total : opp_total;
}

static int requested_bet_size(GameState state, int action) {
	switch (action) {
		case B10:
			return (int)(state.pot * 0.1f);
		case B25:
			return (int)(state.pot * 0.25f);
		case B52:
			return (int)(state.pot * 0.52f);
		case B100:
			return state.pot;
		case B123:
			return (int)(state.pot * 1.23f);
		case R2x:
		case R3x:
		case R4x: {
			int diff = opposing_commit(state) - acting_commit(state);
			int mult = (action == R2x) ? 2 : (action == R3x) ? 3 : 4;

			return diff > 0 ? diff * mult : 0;
		}
		default:
			return 0;
	}
}

static int get_bet_size_for_action(GameState state, int action) {
	if (action == PASS) {
		int diff = opposing_commit(state) - acting_commit(state);
		int target_total = opposing_commit(state);
		int max_total = max_total_commit(state);

		if (diff <= 0)
			return 0;

		if (target_total > max_total)
			target_total = max_total;

		return target_total - acting_commit(state);
	}

	int requested = requested_bet_size(state, action);
	int max_additional = max_total_commit(state) - acting_commit(state);

	if (max_additional <= 0)
		return 0;

	if (requested <= 0)
		requested = 1;

	if (requested > max_additional)
		requested = max_additional;

	return requested;
}

static int seen_bet_size(const int* sizes, int count, int bet_size) {
	for (int i = 0; i < count; i++) {
		if (sizes[i] == bet_size)
			return 1;
	}

	return 0;
}

void train(PublicNode* root, GameState initial_state, int num_combos, float* p1_starting_range, float* p2_starting_range, int iteration) {
	float* p1_reach  = (float*)malloc(num_combos * sizeof(float));
	float* p2_reach  = (float*)malloc(num_combos * sizeof(float));
	float* root_util = (float*)malloc(num_combos * sizeof(float));

	for (int i = 0; i < num_combos; i++) {
		p1_reach[i] = p1_starting_range[i];
		p2_reach[i] = p2_starting_range[i];
		root_util[i] = 0.0f;
	}

	//alpha = 1.5f;
	//beta =  0.5f;
	//gamma = 2.0f;
	//this is recommended from the DCFR (2019) paper
	if (iteration > 1)
		discount_tree(root, num_combos, iteration, 1.5f, 0.5f, 2.0f);
	dcfr(root, initial_state, num_combos, p1_reach, p2_reach, root_util);

	free(p1_reach);
	free(p2_reach);
	free(root_util);
}

int get_legal_actions(GameState state, int8_t* out_actions) {
	uint8_t act = 0;
	int facing_bet = has_outstanding_bet(state);

	if (facing_bet)
		out_actions[act++] = FOLD;

	out_actions[act++] = PASS;

	if (facing_bet) {
		if (state.raises_this_street < 2) {
			int raise_actions[] = {R2x, R3x, R4x};
			int seen_sizes[3];
			int seen_count = 0;

			for (int i = 0; i < 3; i++) {
				int bet_size = get_bet_size_for_action(state, raise_actions[i]);

				if (bet_size <= 0)
					continue;

				if (seen_bet_size(seen_sizes, seen_count, bet_size))
					continue;

				seen_sizes[seen_count++] = bet_size;
				out_actions[act++] = raise_actions[i];
			}
		}
	}
	else {
		int aggressive_actions[] = {B10, B25, B52, B100, B123};
		int seen_sizes[5];
		int seen_count = 0;

		for (int i = 0; i < 5; i++) {
			int bet_size = get_bet_size_for_action(state, aggressive_actions[i]);

			if (bet_size <= 0)
				continue;

			if (seen_bet_size(seen_sizes, seen_count, bet_size))
				continue;

			seen_sizes[seen_count++] = bet_size;
			out_actions[act++] = aggressive_actions[i];
		}
	}

	return act;
}

void calculate_strategy(float* regret_sum, float* strategy, int num_actions, int num_combos) {
	/*
	 * [[b1,b2,b3],[b1,b2,b3][..]...] flattened, regret_sum[action][bucket]
	 * We're getting positive regret_sum and prepping them for action weighting 
	 */
	for (int a = 0; a < num_actions; a++) {
		for (int combo = 0 ; combo < num_combos; combo++) {
			int id = (a * num_combos) + combo;
			float r = regret_sum[id];
			strategy[id] = r <= 0.0f ? 0 : r;
		}
	}

	//per combo, for each action, coming up with a strategy (0.0-1.0) based on normalizing the regrets for a specific combo
	for (int combo = 0; combo < num_combos; combo++) {
		float sum = 0.0f;

		for (int a = 0; a < num_actions; a++)
			sum += strategy[(a * num_combos) + combo];
		
		//normalize strategy
		for (int a = 0; a < num_actions; a++) {
			int id = (a * num_combos) + combo;
			strategy[id] = sum > 0.0f ? 
				strategy[id] / sum :
				1.0f / (float)num_actions;
		}
	}
}

GameState apply_bet(GameState current_state, int action) {
	GameState next_state = current_state;
	next_state.num_actions_this_street += 1;
	int bet_size;

	if (action == FOLD) {
		next_state.last_action_was_fold = 1;
		return next_state;
	}

	bet_size = get_bet_size_for_action(current_state, action);

	switch (action) {
		case PASS:
		case B10:
		case B25:
		case B52:
		case B100:
		case B123:
			break;
		case R2x:
		case R3x:
		case R4x:
			next_state.raises_this_street += 1;
			break;
		default:
			printf("ERR: BAD ACTION IN APPLY_BET %d\n", action);
			return next_state;
	}
	
	if (next_state.active_player == 0) {
		next_state.p1_commit += bet_size;
		next_state.p1_stack  -= bet_size;
	}
	else {
		next_state.p2_commit += bet_size;
		next_state.p2_stack  -= bet_size;
	}

	next_state.pot += bet_size;

	next_state.active_player = 1 - next_state.active_player;
	
	return next_state;
}

GameState apply_deal(GameState current_state, int card_idx) {
	GameState next_state = current_state;

	int rank = card_idx % 13;
	int suit = card_idx / 13;

	uint64_t new_card_mask = 1ULL << (rank + (suit * 16));

	next_state.board |= new_card_mask;

	next_state.street += 1;
	
	next_state.p1_commit = 0;
	next_state.p2_commit = 0;
	next_state.raises_this_street = 0;
	next_state.num_actions_this_street = 0;
	next_state.last_action_was_fold = 0;

	next_state.active_player = 0; //oop always acts first
	
	return next_state;
}


void action_node(PublicNode* node, GameState state, int num_combos, float* p1_reach, float* p2_reach, float* expected_util) {
	int8_t legal_actions[MAX_LEGAL_ACTIONS];
	uint8_t active = node->active_player;
	int action_count = get_legal_actions(state, legal_actions);
	ActionFrame* frame = push_action_frame(num_combos, action_count);
	float* strategy;
	float* action_expected_util;

	if (!frame) {
		printf("ERR: action scratch alloc/depth failed\n");
		return;
	}

	strategy = frame->strategy;
	action_expected_util = frame->action_util;
	memset(action_expected_util, 0, (size_t)action_count * (size_t)num_combos * sizeof(float));
	memset(expected_util, 0, (size_t)num_combos * sizeof(float));

	calculate_strategy(node->regret_sum, strategy, action_count, num_combos);

	for (int action = 0; action < action_count; action++) {
		float* next_p1_reach = frame->next_p1;
		float* next_p2_reach = frame->next_p2;

		memcpy(next_p1_reach, p1_reach, sizeof(float) * (size_t)num_combos);
		memcpy(next_p2_reach, p2_reach, sizeof(float) * (size_t)num_combos);

		/*
		 * We're updating the reach probability for each player, for a given action, for each combo
		 * We have our initial weightings due to preflop ranges, and then certain hands will not have
		 * the same weighting due cards literally being better than others. 72o will not be weighted
		 * the same way as AA.
		 */
		for (int combo = 0; combo < num_combos; combo++) {
			int idx = (action * num_combos) + combo;
			if (active == 0)
				next_p1_reach[combo] *= strategy[idx];
			else
				next_p2_reach[combo] *= strategy[idx];
		}

		GameState next_state = apply_bet(state, legal_actions[action]);

		//we calculate utility on a per-action basis, per node
		float* child_expected_util = &action_expected_util[action * num_combos];
		dcfr(node->children[action], next_state, num_combos, next_p1_reach, next_p2_reach, child_expected_util);

		/*
		 * update utility, the child node is from perspective of next player so we must reverse it
		 * expected_util[combo] += strategy[action][combo] * child_expected_util[combo]
		 * we are getting the expected_util by the EV of the next node for our selected combos for the node (on a per action basis)
		 * this is converted into the perspective of our node
		 * we multiply it by strategy since we're choosing this action with this combo 100% of the time, only a certain broken down percentage
		*/
		for (int combo = 0; combo < num_combos; combo++) {
			child_expected_util[combo] = -child_expected_util[combo];
			expected_util[combo] += strategy[(action * num_combos) + combo] * child_expected_util[combo];
		}
	}

	/*
	 * Update Regrets
	 * regret = action EV for combo - average EV for combo
	 * we update reaches since we're not hitting every combo 100% of the time
	 * We update regret based on the chance the opponent will take an combo of an action
	 * We update strategy based on the chance we'll perform a specific combo of an action
	 */
	for (int action = 0; action < action_count; action++) {
		for (int combo = 0; combo < num_combos; combo++) {
			int id = (action * num_combos) + combo;

			float regret = action_expected_util[id] - expected_util[combo];

			float opp_reach = (active == 0) ? p2_reach[combo] : p1_reach[combo];
			float my_reach  = (active == 0) ? p1_reach[combo] : p2_reach[combo];

			node->regret_sum[id] += regret * opp_reach;
			node->strategy_sum[id] += strategy[id] * my_reach;
		}
	}

	pop_action_frame();
}

void chance_node(PublicNode* node, GameState state, int num_combos, float* p1_reach, float* p2_reach, float* expected_util) {
	ensure_combo_masks(num_combos);
	init_combo_suit_permutations(num_combos);
	memset(expected_util, 0, (size_t)num_combos * sizeof(float));

	int physical_card_count = 52 - __builtin_popcountll(state.board);
	float weight = physical_card_count ? 1.0f / (float)physical_card_count : 0.0f;
	int parallel = node->num_children > 4 && !omp_in_parallel();

	#pragma omp parallel if(parallel)
	{
		ChanceScratch* scratch = &chance_scratch;
		float* child_expected_util;
		float* next_p1_reach;
		float* next_p2_reach;
		float* local_util;

		if (!ensure_chance_scratch(num_combos)) {
			#pragma omp critical
			printf("ERR: chance scratch alloc failed\n");
		}
		else {
			child_expected_util = scratch->child_util;
			next_p1_reach = scratch->next_p1;
			next_p2_reach = scratch->next_p2;
			local_util = scratch->local_util;
			memset(local_util, 0, (size_t)num_combos * sizeof(float));

			#pragma omp for schedule(dynamic)
			for (int i = 0; i < node->num_children; i++) {
				uint8_t representative = node->dealt_cards[i];
				uint8_t physical_cards[4];
				int orbit_size = collect_isomorphic_card_orbit(
					state.board,
					representative,
					physical_cards
				);
				int representative_suit = representative / 13;
				GameState next_state = apply_deal(state, representative);
				float util_scale = orbit_size > 0 ? weight / (float)orbit_size : 0.0f;

				/*
				 * One shared subtree per orbit. Sum mapped reaches from every
				 * physical card into the representative frame, traverse once
				 * (so regrets update once with total CF reach), then map EV
				 * back with weight/(orbit_size) so total mass stays 1/N each.
				 */
				memset(next_p1_reach, 0, (size_t)num_combos * sizeof(float));
				memset(next_p2_reach, 0, (size_t)num_combos * sizeof(float));

				for (int orbit_index = 0; orbit_index < orbit_size; orbit_index++) {
					int physical_suit = physical_cards[orbit_index] / 13;

					for (int combo = 0; combo < num_combos; combo++) {
						int canonical_combo = permute_combo_suits(
							combo,
							physical_suit,
							representative_suit
						);

						if (combo_is_dead(combo_masks[canonical_combo], next_state.board))
							continue;

						next_p1_reach[canonical_combo] += p1_reach[combo];
						next_p2_reach[canonical_combo] += p2_reach[combo];
					}
				}

				dcfr(
					node->children[i],
					next_state,
					num_combos,
					next_p1_reach,
					next_p2_reach,
					child_expected_util
				);

				for (int orbit_index = 0; orbit_index < orbit_size; orbit_index++) {
					int physical_suit = physical_cards[orbit_index] / 13;

					for (int combo = 0; combo < num_combos; combo++) {
						int canonical_combo = permute_combo_suits(
							combo,
							physical_suit,
							representative_suit
						);

						local_util[combo] += child_expected_util[canonical_combo] * util_scale;
					}
				}
			}

			#pragma omp critical
			for (int combo = 0; combo < num_combos; combo++)
				expected_util[combo] += local_util[combo];
		}
	}
}

void terminal_node(GameState state, int num_combos, float* p1_reach, float* p2_reach, float* expected_util) {
	ensure_combo_masks(num_combos);

	//we're finally setting the EV
	memset(expected_util, 0, num_combos * sizeof(float));	

	//It's really simple if we have a fold, we're just counting commits
	if (state.last_action_was_fold) {
		for (int combo = 0; combo < num_combos; combo++) {
			if (combo_is_dead(combo_masks[combo], state.board))
				continue;

			if (state.active_player == 0)
				expected_util[combo] = -(float)state.p1_commit;
			else
				expected_util[combo] = -(float)state.p2_commit;
		}
		return;
	}

	int combo_scores[MAX_COMBOS] = {0};
	for (int combo = 0; combo < num_combos; combo++) {
		if (combo_is_dead(combo_masks[combo], state.board))
			continue;

		combo_scores[combo] = evaluate_board(combo_masks[combo], state.board);
	}

	for (int p1c = 0; p1c < num_combos; p1c++) {
		if (p1_reach[p1c] == 0.0f)
			continue;

		if (combo_is_dead(combo_masks[p1c], state.board))
			continue;

		float ev = 0.0f;

		for (int p2c = 0; p2c < num_combos; p2c++) {
			if (p2_reach[p2c] == 0.0f)
				continue;

			if (combo_is_dead(combo_masks[p2c], state.board))
				continue;

			if (combo_masks[p1c] & combo_masks[p2c])
				continue;

			int p1_score = combo_scores[p1c];
			int p2_score = combo_scores[p2c];

			if (p1_score > p2_score)
				ev += p2_reach[p2c] * (float)state.p2_commit;
			else if (p2_score > p1_score)
				ev -= p2_reach[p2c] * (float)state.p1_commit;
		}

		if (state.active_player == 0)
			expected_util[p1c] = ev;
		else
			expected_util[p1c] = -ev;
	}
}



void dcfr(PublicNode* node, GameState state, int num_combos, float* p1_reach, float* p2_reach, float* expected_util) {
	if (node->type == NODE_ACTION)
		action_node(node, state, num_combos, p1_reach, p2_reach, expected_util);
	else if (node->type == NODE_CHANCE)
		chance_node(node, state, num_combos, p1_reach, p2_reach, expected_util);
	else if (node->type == NODE_TERMINAL)
		terminal_node(state, num_combos, p1_reach, p2_reach, expected_util);
}

void discount_tree(PublicNode* node, int num_combos, int t, float alpha, float beta, float gamma) {
	if (node->type == NODE_TERMINAL)
		return;

	if (node->type == NODE_ACTION) {
		float pos_disc = powf((float)t, alpha) / (powf((float)t, alpha) + 1.0f);
		float neg_disc = powf((float)t, beta)  / (powf((float)t, beta)  + 1.0f);
		float strat_disc = powf((float)t / ((float)t + 1.0f), gamma);

		int total = node->num_children * num_combos;
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
		discount_tree(node->children[i], num_combos, t, alpha, beta, gamma);
}
