#include "../dcfr.h"
#include "../isomorphism.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define NUM_COMBOS 1326
#define EPSILON 1e-4f

static uint64_t card_mask(int rank, int suit) {
	return 1ULL << (rank + suit * 16);
}

static int combo_index(uint64_t mask) {
	for (int combo = 0; combo < NUM_COMBOS; combo++) {
		if (get_mask_for_combo(combo) == mask)
			return combo;
	}
	return -1;
}

static void assert_close(float actual, float expected) {
	assert(fabsf(actual - expected) < EPSILON);
}

static float range_weighted_value(const float* reach, const float* util) {
	float value = 0.0f;

	for (int combo = 0; combo < NUM_COMBOS; combo++)
		value += reach[combo] * util[combo];
	return value;
}

static GameState river_state(uint64_t board) {
	return (GameState){
		.board = board,
		.pot = 200,
		.p1_stack = 900,
		.p2_stack = 900,
		.p1_commit = 0,
		.p2_commit = 0,
		.p1_invested = 100,
		.p2_invested = 100,
		.active_player = P1,
		.street = 2,
		.raises_this_street = 0,
		.num_actions_this_street = 2,
		.last_action_was_fold = 0,
	};
}

static void test_showdown_utilities(void) {
	uint64_t board =
		card_mask(0, 0) |  /* 2s */
		card_mask(1, 1) |  /* 3h */
		card_mask(2, 2) |  /* 4d */
		card_mask(3, 3) |  /* 5c */
		card_mask(7, 0);   /* 9s */
	int wheel = combo_index(card_mask(12, 0) | card_mask(12, 2)); /* As Ad */
	int kings = combo_index(card_mask(11, 1) | card_mask(11, 2)); /* Kh Kd */
	float* p1_reach = (float*)calloc(NUM_COMBOS, sizeof(float));
	float* p2_reach = (float*)calloc(NUM_COMBOS, sizeof(float));
	float* p1_util = (float*)calloc(NUM_COMBOS, sizeof(float));
	float* p2_util = (float*)calloc(NUM_COMBOS, sizeof(float));
	GameState state = river_state(board);

	assert(wheel >= 0 && kings >= 0);
	p1_reach[wheel] = 0.4f;
	p2_reach[kings] = 0.7f;
	terminal_node(state, NUM_COMBOS, p1_reach, p2_reach, p1_util, p2_util);

	// Each player's vector is indexed by that player's own hand and integrates
	// the compatible opponent reach.
	assert_close(p1_util[wheel], 70.0f);
	assert_close(p2_util[kings], -40.0f);
	assert_close(
		range_weighted_value(p1_reach, p1_util) +
			range_weighted_value(p2_reach, p2_util),
		0.0f
	);

	// Swapping the players swaps and negates the corresponding utilities.
	memset(p1_reach, 0, NUM_COMBOS * sizeof(float));
	memset(p2_reach, 0, NUM_COMBOS * sizeof(float));
	memset(p1_util, 0, NUM_COMBOS * sizeof(float));
	memset(p2_util, 0, NUM_COMBOS * sizeof(float));
	p1_reach[kings] = 0.7f;
	p2_reach[wheel] = 0.4f;
	terminal_node(state, NUM_COMBOS, p1_reach, p2_reach, p1_util, p2_util);
	assert_close(p1_util[kings], -40.0f);
	assert_close(p2_util[wheel], 70.0f);

	free(p1_reach);
	free(p2_reach);
	free(p1_util);
	free(p2_util);
}

static void test_fold_utilities_and_blockers(void) {
	uint64_t board =
		card_mask(0, 0) |
		card_mask(1, 1) |
		card_mask(2, 2) |
		card_mask(3, 3) |
		card_mask(7, 0);
	int aces = combo_index(card_mask(12, 0) | card_mask(12, 2));   /* As Ad */
	int kings = combo_index(card_mask(11, 1) | card_mask(11, 2)); /* Kh Kd */
	int blocked = combo_index(card_mask(12, 0) | card_mask(10, 1)); /* As Qh */
	float* p1_reach = (float*)calloc(NUM_COMBOS, sizeof(float));
	float* p2_reach = (float*)calloc(NUM_COMBOS, sizeof(float));
	float* p1_util = (float*)calloc(NUM_COMBOS, sizeof(float));
	float* p2_util = (float*)calloc(NUM_COMBOS, sizeof(float));
	GameState state = river_state(board);

	state.active_player = P1;
	state.last_action_was_fold = 1;
	state.p1_commit = 50;
	state.p2_commit = 100;
	state.pot = 150;
	state.p1_invested = 50;
	state.p2_invested = 100;
	p1_reach[aces] = 0.4f;
	p2_reach[kings] = 0.7f;
	p2_reach[blocked] = 0.9f;
	terminal_node(state, NUM_COMBOS, p1_reach, p2_reach, p1_util, p2_util);

	// The blocked AsQh hand contributes no reach against AsAd.
	assert_close(p1_util[aces], -35.0f);
	assert_close(p2_util[kings], 20.0f);
	assert_close(p2_util[blocked], 0.0f);
	assert_close(
		range_weighted_value(p1_reach, p1_util) +
			range_weighted_value(p2_reach, p2_util),
		0.0f
	);

	free(p1_reach);
	free(p2_reach);
	free(p1_util);
	free(p2_util);
}

static void test_suit_isomorphic_terminal(void) {
	uint64_t board =
		card_mask(0, 0) |
		card_mask(1, 1) |
		card_mask(2, 2) |
		card_mask(3, 3) |
		card_mask(7, 0);
	int p1_hand = combo_index(card_mask(12, 0) | card_mask(12, 2));
	int p2_hand = combo_index(card_mask(11, 1) | card_mask(11, 2));
	int mapped_p1;
	int mapped_p2;
	uint64_t mapped_board = 0;
	float* p1_reach = (float*)calloc(NUM_COMBOS, sizeof(float));
	float* p2_reach = (float*)calloc(NUM_COMBOS, sizeof(float));
	float* p1_util = (float*)calloc(NUM_COMBOS, sizeof(float));
	float* p2_util = (float*)calloc(NUM_COMBOS, sizeof(float));
	GameState state = river_state(board);
	GameState mapped_state;

	init_combo_suit_permutations(NUM_COMBOS);
	mapped_p1 = permute_combo_suits(p1_hand, 0, 1);
	mapped_p2 = permute_combo_suits(p2_hand, 0, 1);
	for (int card = 0; card < 52; card++) {
		int rank = card % 13;
		int suit = card / 13;
		int mapped_suit = suit == 0 ? 1 : (suit == 1 ? 0 : suit);

		if (board & card_mask(rank, suit))
			mapped_board |= card_mask(rank, mapped_suit);
	}

	p1_reach[p1_hand] = 0.4f;
	p2_reach[p2_hand] = 0.7f;
	terminal_node(state, NUM_COMBOS, p1_reach, p2_reach, p1_util, p2_util);
	float original_p1 = p1_util[p1_hand];
	float original_p2 = p2_util[p2_hand];

	memset(p1_reach, 0, NUM_COMBOS * sizeof(float));
	memset(p2_reach, 0, NUM_COMBOS * sizeof(float));
	memset(p1_util, 0, NUM_COMBOS * sizeof(float));
	memset(p2_util, 0, NUM_COMBOS * sizeof(float));
	p1_reach[mapped_p1] = 0.4f;
	p2_reach[mapped_p2] = 0.7f;
	mapped_state = river_state(mapped_board);
	terminal_node(
		mapped_state,
		NUM_COMBOS,
		p1_reach,
		p2_reach,
		p1_util,
		p2_util
	);
	assert_close(p1_util[mapped_p1], original_p1);
	assert_close(p2_util[mapped_p2], original_p2);

	free(p1_reach);
	free(p2_reach);
	free(p1_util);
	free(p2_util);
}

static void test_investment_survives_street_transition(void) {
	GameState state = {
		.board = card_mask(12, 0) | card_mask(9, 1) | card_mask(4, 2),
		.pot = 300,
		.p1_stack = 850,
		.p2_stack = 850,
		.p1_commit = 50,
		.p2_commit = 50,
		.p1_invested = 150,
		.p2_invested = 150,
		.active_player = P1,
		.street = 0,
		.raises_this_street = 0,
		.num_actions_this_street = 2,
		.last_action_was_fold = 0,
	};
	GameState turn = apply_deal(state, 8); /* Ts */
	GameState bet = apply_bet(turn, B10);

	assert(turn.p1_commit == 0 && turn.p2_commit == 0);
	assert(turn.p1_invested == 150 && turn.p2_invested == 150);
	assert(turn.pot == 300);
	assert(bet.p1_commit == 30);
	assert(bet.p1_invested == 180);
	assert(bet.p2_invested == 150);
	assert(bet.pot == 330);
}

static float train_facing_bet(int p1_hand, int p2_hand, uint64_t board) {
	PublicNode fold = {.type = NODE_TERMINAL, .active_player = P2};
	PublicNode call = {.type = NODE_TERMINAL, .active_player = P1};
	PublicNode* children[2] = {&fold, &call};
	float* regrets = (float*)calloc(2 * NUM_COMBOS, sizeof(float));
	float* strategy_sum = (float*)calloc(2 * NUM_COMBOS, sizeof(float));
	float* p1_range = (float*)calloc(NUM_COMBOS, sizeof(float));
	float* p2_range = (float*)calloc(NUM_COMBOS, sizeof(float));
	PublicNode root = {
		.type = NODE_ACTION,
		.active_player = P2,
		.num_children = 2,
		.children = children,
		.regret_sum = regrets,
		.strategy_sum = strategy_sum,
	};
	GameState state = {
		.board = board,
		.pot = 300,
		.p1_stack = 800,
		.p2_stack = 900,
		.p1_commit = 100,
		.p2_commit = 0,
		.p1_invested = 200,
		.p2_invested = 100,
		.active_player = P2,
		.street = 2,
		.raises_this_street = 0,
		.num_actions_this_street = 1,
		.last_action_was_fold = 0,
	};
	float total;
	float call_probability;

	p1_range[p1_hand] = 1.0f;
	p2_range[p2_hand] = 1.0f;
	for (int iteration = 1; iteration <= 500; iteration++)
		train(&root, state, NUM_COMBOS, p1_range, p2_range, iteration);

	total = strategy_sum[p2_hand] + strategy_sum[NUM_COMBOS + p2_hand];
	call_probability = strategy_sum[NUM_COMBOS + p2_hand] / total;

	free(regrets);
	free(strategy_sum);
	free(p1_range);
	free(p2_range);
	return call_probability;
}

static void test_known_facing_bet_actions(void) {
	uint64_t board =
		card_mask(0, 0) |
		card_mask(1, 1) |
		card_mask(2, 2) |
		card_mask(3, 3) |
		card_mask(7, 0);
	int wheel = combo_index(card_mask(12, 0) | card_mask(12, 2));
	int kings = combo_index(card_mask(11, 1) | card_mask(11, 2));
	float winning_call;
	float losing_call;

	assert(set_max_raises(0));
	// In this one-decision reference game, the wheel wins the carried pot by
	// calling, while folding loses its prior investment.
	winning_call = train_facing_bet(kings, wheel, board);
	// Reversing the hands makes calling lose an additional 100 chips, so fold
	// must dominate the average strategy.
	losing_call = train_facing_bet(wheel, kings, board);
	if (!(winning_call > 0.99f && losing_call < 0.03f))
		fprintf(stderr, "known game call probabilities: winner=%f loser=%f\n",
			winning_call, losing_call);
	assert(winning_call > 0.99f);
	assert(losing_call < 0.03f);
	assert(set_max_raises(2));
}

static int contains_action(const int8_t* actions, int count, int action) {
	for (int i = 0; i < count; i++) {
		if (actions[i] == action)
			return 1;
	}
	return 0;
}

static void test_configured_sizes_and_allin(void) {
	float bets[] = {0.40f, 1.00f};
	float raises[] = {3.0f};
	float default_bets[] = {0.10f, 0.25f, 0.52f, 1.00f, 1.23f};
	float default_raises[] = {2.0f, 3.0f, 4.0f};
	int8_t actions[MAX_LEGAL_ACTIONS];
	GameState state = {
		.pot = 200,
		.p1_stack = 900,
		.p2_stack = 900,
		.p1_invested = 100,
		.p2_invested = 100,
		.active_player = P1,
	};

	assert(set_bet_sizes(bets, 2));
	assert(set_raise_sizes(raises, 1));
	assert(set_max_raises(1));
	set_allin_enabled(1);

	int count = get_legal_actions(state, actions);
	assert(count == 4);
	assert(contains_action(actions, count, PASS));
	assert(contains_action(actions, count, B10));
	assert(contains_action(actions, count, B25));
	assert(contains_action(actions, count, ALLIN));

	GameState jam = apply_bet(state, ALLIN);
	assert(jam.p1_stack == 0);
	assert(jam.p1_commit == 900);
	assert(jam.p1_invested == 1000);
	assert(jam.pot == 1100);

	count = get_legal_actions(jam, actions);
	assert(count == 2);
	assert(contains_action(actions, count, FOLD));
	assert(contains_action(actions, count, PASS));

	GameState bet = apply_bet(state, B10);
	count = get_legal_actions(bet, actions);
	assert(count == 4);
	assert(contains_action(actions, count, FOLD));
	assert(contains_action(actions, count, PASS));
	assert(contains_action(actions, count, R2x));
	assert(contains_action(actions, count, ALLIN));

	assert(set_bet_sizes(default_bets, 5));
	assert(set_raise_sizes(default_raises, 3));
	assert(set_max_raises(2));
	set_allin_enabled(0);
}

int main(void) {
	init_evaluator();
	test_showdown_utilities();
	test_fold_utilities_and_blockers();
	test_suit_isomorphic_terminal();
	test_investment_survives_street_transition();
	test_known_facing_bet_actions();
	test_configured_sizes_and_allin();
	printf("dcfr tests passed\n");
	return 0;
}
