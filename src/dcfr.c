#include "dcfr.h"
static float g_bet_sizes[MAX_BET_SIZES] = {0.10f, 0.25f, 0.52f, 1.00f, 1.23f};
static float g_raise_sizes[MAX_RAISE_SIZES] = {2.0f, 3.0f, 4.0f};
static int g_bet_count = MAX_BET_SIZES;
static int g_raise_count = MAX_RAISE_SIZES;
static int g_max_raises = 2;
static int g_allin_enabled = 0;

int set_bet_sizes(const float* pot_fractions, int count) {
	if (!pot_fractions || count < 1 || count > MAX_BET_SIZES)
		return 0;
	for (int i = 0; i < count; i++) {
		if (pot_fractions[i] <= 0.0f)
			return 0;
		g_bet_sizes[i] = pot_fractions[i];
	}
	g_bet_count = count;
	return 1;
}

int set_raise_sizes(const float* multipliers, int count) {
	if (!multipliers || count < 1 || count > MAX_RAISE_SIZES)
		return 0;
	for (int i = 0; i < count; i++) {
		if (multipliers[i] <= 1.0f)
			return 0;
		g_raise_sizes[i] = multipliers[i];
	}
	g_raise_count = count;
	return 1;
}

int set_max_raises(int max_raises) {
	if (max_raises < 0 || max_raises > 2)
		return 0;
	g_max_raises = max_raises;
	return 1;
}

void set_allin_enabled(int enabled) {
	g_allin_enabled = enabled != 0;
}

float action_size_value(int action) {
	if (action >= B10 && action <= B123)
		return g_bet_sizes[action - B10] * 100.0f;
	if (action >= R2x && action <= R4x)
		return g_raise_sizes[action - R2x];
	return 0.0f;
}

int configured_bet_count(void) { return g_bet_count; }
int configured_raise_count(void) { return g_raise_count; }
int configured_max_raises(void) { return g_max_raises; }

#include "isomorphism.h"

#include <stdlib.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#define MAX_COMBOS 1326
#define MAX_ACTION_DEPTH 64

static uint64_t combo_masks[MAX_COMBOS];
static int combo_masks_ready = 0;

typedef struct {
	float* strategy;
	float* p1_action_util;
	float* p2_action_util;
	float* next_p1;
	float* next_p2;
	int capacity_combos;
	int capacity_actions;
} ActionFrame;

typedef struct {
	float* next_p1;
	float* next_p2;
	float* p1_child_util;
	float* p2_child_util;
	float* p1_local_util;
	float* p2_local_util;
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
		free(frame->p1_action_util);
		free(frame->p2_action_util);
		frame->strategy = (float*)malloc(action_bytes);
		frame->p1_action_util = (float*)malloc(action_bytes);
		frame->p2_action_util = (float*)malloc(action_bytes);
		if (!frame->strategy || !frame->p1_action_util || !frame->p2_action_util)
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
	free(s->p1_child_util);
	free(s->p2_child_util);
	free(s->p1_local_util);
	free(s->p2_local_util);
	s->next_p1 = (float*)malloc((size_t)num_combos * sizeof(float));
	s->next_p2 = (float*)malloc((size_t)num_combos * sizeof(float));
	s->p1_child_util = (float*)malloc((size_t)num_combos * sizeof(float));
	s->p2_child_util = (float*)malloc((size_t)num_combos * sizeof(float));
	s->p1_local_util = (float*)malloc((size_t)num_combos * sizeof(float));
	s->p2_local_util = (float*)malloc((size_t)num_combos * sizeof(float));
	if (!s->next_p1 || !s->next_p2 || !s->p1_child_util ||
		!s->p2_child_util || !s->p1_local_util || !s->p2_local_util)
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
	if (action >= B10 && action <= B123)
		return (int)(state.pot * g_bet_sizes[action - B10]);
	if (action >= R2x && action <= R4x) {
		int diff = opposing_commit(state) - acting_commit(state);
		return diff > 0 ? (int)(diff * g_raise_sizes[action - R2x]) : 0;
	}
	return 0;
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
	if (action == ALLIN)
		return max_additional;

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
	float* p1_root_util = (float*)malloc(num_combos * sizeof(float));
	float* p2_root_util = (float*)malloc(num_combos * sizeof(float));

	for (int i = 0; i < num_combos; i++) {
		p1_reach[i] = p1_starting_range[i];
		p2_reach[i] = p2_starting_range[i];
		p1_root_util[i] = 0.0f;
		p2_root_util[i] = 0.0f;
	}

	//alpha = 1.5f;
	//beta =  0.5f;
	//gamma = 2.0f;
	//this is recommended from the DCFR (2019) paper
	if (iteration > 1)
		discount_tree(root, num_combos, iteration, 1.5f, 0.5f, 2.0f);
	dcfr(root, initial_state, num_combos, p1_reach, p2_reach, p1_root_util, p2_root_util);

	free(p1_reach);
	free(p2_reach);
	free(p1_root_util);
	free(p2_root_util);
}

int get_legal_actions(GameState state, int8_t* out_actions) {
	uint8_t act = 0;
	int facing_bet = has_outstanding_bet(state);

	if (facing_bet)
		out_actions[act++] = FOLD;

	out_actions[act++] = PASS;

	if (facing_bet) {
		if (state.raises_this_street < g_max_raises) {
			int raise_actions[] = {R2x, R3x, R4x};
			int seen_sizes[MAX_RAISE_SIZES + 1];
			int seen_count = 0;
			int call_size = get_bet_size_for_action(state, PASS);

			for (int i = 0; i < g_raise_count; i++) {
				int bet_size = get_bet_size_for_action(state, raise_actions[i]);

				if (bet_size <= call_size)
					continue;

				if (seen_bet_size(seen_sizes, seen_count, bet_size))
					continue;

				seen_sizes[seen_count++] = bet_size;
				out_actions[act++] = raise_actions[i];
			}

			int allin_size = get_bet_size_for_action(state, ALLIN);
			if (g_allin_enabled && allin_size > call_size &&
				!seen_bet_size(seen_sizes, seen_count, allin_size))
				out_actions[act++] = ALLIN;
		}
	}
	else {
		int aggressive_actions[] = {B10, B25, B52, B100, B123};
		int seen_sizes[MAX_BET_SIZES + 1];
		int seen_count = 0;

		for (int i = 0; i < g_bet_count; i++) {
			int bet_size = get_bet_size_for_action(state, aggressive_actions[i]);

			if (bet_size <= 0)
				continue;

			if (seen_bet_size(seen_sizes, seen_count, bet_size))
				continue;

			seen_sizes[seen_count++] = bet_size;
			out_actions[act++] = aggressive_actions[i];
		}

		int allin_size = get_bet_size_for_action(state, ALLIN);
		if (g_allin_enabled && allin_size > 0 &&
			!seen_bet_size(seen_sizes, seen_count, allin_size))
			out_actions[act++] = ALLIN;
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
		case ALLIN:
			if (has_outstanding_bet(current_state))
				next_state.raises_this_street += 1;
			break;
		default:
			printf("ERR: BAD ACTION IN APPLY_BET %d\n", action);
			return next_state;
	}
	
	if (next_state.active_player == P1) {
		next_state.p1_commit += bet_size;
		next_state.p1_invested += bet_size;
		next_state.p1_stack  -= bet_size;
	}
	else {
		next_state.p2_commit += bet_size;
		next_state.p2_invested += bet_size;
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

	next_state.active_player = P1; // OOP always acts first
	
	return next_state;
}


void action_node(
	PublicNode* node,
	GameState state,
	int num_combos,
	float* p1_reach,
	float* p2_reach,
	float* p1_util,
	float* p2_util
) {
	int8_t legal_actions[MAX_LEGAL_ACTIONS];
	uint8_t active = node->active_player;
	int action_count = get_legal_actions(state, legal_actions);
	ActionFrame* frame = push_action_frame(num_combos, action_count);
	float* strategy;
	float* p1_action_util;
	float* p2_action_util;

	if (active != state.active_player || action_count != node->num_children) {
		memset(p1_util, 0, (size_t)num_combos * sizeof(float));
		memset(p2_util, 0, (size_t)num_combos * sizeof(float));
		printf("ERR: action node/state mismatch\n");
		if (frame)
			pop_action_frame();
		return;
	}
	if (!frame) {
		printf("ERR: action scratch alloc/depth failed\n");
		return;
	}

	strategy = frame->strategy;
	p1_action_util = frame->p1_action_util;
	p2_action_util = frame->p2_action_util;
	memset(p1_action_util, 0, (size_t)action_count * (size_t)num_combos * sizeof(float));
	memset(p2_action_util, 0, (size_t)action_count * (size_t)num_combos * sizeof(float));
	memset(p1_util, 0, (size_t)num_combos * sizeof(float));
	memset(p2_util, 0, (size_t)num_combos * sizeof(float));

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

		float* child_p1_util = &p1_action_util[action * num_combos];
		float* child_p2_util = &p2_action_util[action * num_combos];
		dcfr(
			node->children[action],
			next_state,
			num_combos,
			next_p1_reach,
			next_p2_reach,
			child_p1_util,
			child_p2_util
		);
	}

	/*
	 * Each utility vector is indexed by that player's own private combo and
	 * already integrates the compatible opponent reach. At the acting player's
	 * node, weight child values by its strategy. For the non-acting player,
	 * child utilities already include the acting player's action reach, so sum
	 * them without applying the strategy a second time.
	 */
	for (int combo = 0; combo < num_combos; combo++) {
		for (int action = 0; action < action_count; action++) {
			int id = (action * num_combos) + combo;

			if (active == P1) {
				p1_util[combo] += strategy[id] * p1_action_util[id];
				p2_util[combo] += p2_action_util[id];
			}
			else {
				p1_util[combo] += p1_action_util[id];
				p2_util[combo] += strategy[id] * p2_action_util[id];
			}
		}
	}

	for (int action = 0; action < action_count; action++) {
		for (int combo = 0; combo < num_combos; combo++) {
			int id = (action * num_combos) + combo;
			float action_value = active == P1 ? p1_action_util[id] : p2_action_util[id];
			float node_value = active == P1 ? p1_util[combo] : p2_util[combo];
			float my_reach = active == P1 ? p1_reach[combo] : p2_reach[combo];

			node->regret_sum[id] += action_value - node_value;
			node->strategy_sum[id] += strategy[id] * my_reach;
		}
	}

	pop_action_frame();
}

void chance_node(
	PublicNode* node,
	GameState state,
	int num_combos,
	float* p1_reach,
	float* p2_reach,
	float* p1_util,
	float* p2_util
) {
	ensure_combo_masks(num_combos);
	init_combo_suit_permutations(num_combos);
	memset(p1_util, 0, (size_t)num_combos * sizeof(float));
	memset(p2_util, 0, (size_t)num_combos * sizeof(float));

	int physical_card_count = 52 - __builtin_popcountll(state.board);
	float weight = physical_card_count ? 1.0f / (float)physical_card_count : 0.0f;
#ifdef _OPENMP
	int parallel = node->num_children > 4 && !omp_in_parallel();
#endif

	#pragma omp parallel if(parallel)
	{
		ChanceScratch* scratch = &chance_scratch;
		float* p1_child_util;
		float* p2_child_util;
		float* next_p1_reach;
		float* next_p2_reach;
		float* p1_local_util;
		float* p2_local_util;

		if (!ensure_chance_scratch(num_combos)) {
			#pragma omp critical
			printf("ERR: chance scratch alloc failed\n");
		}
		else {
			p1_child_util = scratch->p1_child_util;
			p2_child_util = scratch->p2_child_util;
			next_p1_reach = scratch->next_p1;
			next_p2_reach = scratch->next_p2;
			p1_local_util = scratch->p1_local_util;
			p2_local_util = scratch->p2_local_util;
			memset(p1_local_util, 0, (size_t)num_combos * sizeof(float));
			memset(p2_local_util, 0, (size_t)num_combos * sizeof(float));

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
					p1_child_util,
					p2_child_util
				);

				for (int orbit_index = 0; orbit_index < orbit_size; orbit_index++) {
					int physical_suit = physical_cards[orbit_index] / 13;

					for (int combo = 0; combo < num_combos; combo++) {
						int canonical_combo = permute_combo_suits(
							combo,
							physical_suit,
							representative_suit
						);

						p1_local_util[combo] += p1_child_util[canonical_combo] * util_scale;
						p2_local_util[combo] += p2_child_util[canonical_combo] * util_scale;
					}
				}
			}

			#pragma omp critical
			for (int combo = 0; combo < num_combos; combo++) {
				p1_util[combo] += p1_local_util[combo];
				p2_util[combo] += p2_local_util[combo];
			}
		}
	}
}

static void compatible_reach_sums(
	const float* reach,
	uint64_t board,
	int num_combos,
	float* out
) {
	float total = 0.0f;
	float by_card[52] = {0};

	for (int combo = 0; combo < num_combos; combo++) {
		uint64_t mask = combo_masks[combo];
		float weight = reach[combo];

		if (weight == 0.0f || combo_is_dead(mask, board))
			continue;
		total += weight;
		for (int card = 0; card < 52; card++) {
			int rank = card % 13;
			int suit = card / 13;

			if (mask & (1ULL << (rank + suit * 16)))
				by_card[card] += weight;
		}
	}

	for (int combo = 0; combo < num_combos; combo++) {
		uint64_t mask = combo_masks[combo];
		float compatible = total;

		if (combo_is_dead(mask, board)) {
			out[combo] = 0.0f;
			continue;
		}
		for (int card = 0; card < 52; card++) {
			int rank = card % 13;
			int suit = card / 13;

			if (mask & (1ULL << (rank + suit * 16)))
				compatible -= by_card[card];
		}
		// The identical two-card combo was subtracted once for each card.
		compatible += reach[combo];
		out[combo] = compatible > 0.0f ? compatible : 0.0f;
	}
}

void terminal_node(
	GameState state,
	int num_combos,
	float* p1_reach,
	float* p2_reach,
	float* p1_util,
	float* p2_util
) {
	ensure_combo_masks(num_combos);

	memset(p1_util, 0, (size_t)num_combos * sizeof(float));
	memset(p2_util, 0, (size_t)num_combos * sizeof(float));

	if (state.last_action_was_fold) {
		float* p1_compatible = (float*)malloc((size_t)num_combos * sizeof(float));
		float* p2_compatible = (float*)malloc((size_t)num_combos * sizeof(float));
		float p1_payoff = state.active_player == P1
			? -(float)state.p1_invested
			: (float)state.p2_invested;

		if (!p1_compatible || !p2_compatible) {
			free(p1_compatible);
			free(p2_compatible);
			printf("ERR: terminal fold scratch alloc failed\n");
			return;
		}
		compatible_reach_sums(p1_reach, state.board, num_combos, p1_compatible);
		compatible_reach_sums(p2_reach, state.board, num_combos, p2_compatible);

		for (int combo = 0; combo < num_combos; combo++) {
			if (combo_is_dead(combo_masks[combo], state.board))
				continue;
			p1_util[combo] = p2_compatible[combo] * p1_payoff;
			p2_util[combo] = p1_compatible[combo] * -p1_payoff;
		}
		free(p1_compatible);
		free(p2_compatible);
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

		for (int p2c = 0; p2c < num_combos; p2c++) {
			if (p2_reach[p2c] == 0.0f)
				continue;

			if (combo_is_dead(combo_masks[p2c], state.board))
				continue;

			if (combo_masks[p1c] & combo_masks[p2c])
				continue;

			int p1_score = combo_scores[p1c];
			int p2_score = combo_scores[p2c];
			float p1_payoff =
				((float)state.p2_invested - (float)state.p1_invested) * 0.5f;

			if (p1_score > p2_score)
				p1_payoff = (float)state.p2_invested;
			else if (p2_score > p1_score)
				p1_payoff = -(float)state.p1_invested;

			p1_util[p1c] += p2_reach[p2c] * p1_payoff;
			p2_util[p2c] += p1_reach[p1c] * -p1_payoff;
		}
	}
}



void dcfr(
	PublicNode* node,
	GameState state,
	int num_combos,
	float* p1_reach,
	float* p2_reach,
	float* p1_util,
	float* p2_util
) {
	if (node->type == NODE_ACTION)
		action_node(node, state, num_combos, p1_reach, p2_reach, p1_util, p2_util);
	else if (node->type == NODE_CHANCE)
		chance_node(node, state, num_combos, p1_reach, p2_reach, p1_util, p2_util);
	else if (node->type == NODE_TERMINAL)
		terminal_node(state, num_combos, p1_reach, p2_reach, p1_util, p2_util);
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
