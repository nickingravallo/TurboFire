#include "gto_solver.h"
#include "range.h"
#include "ranks.h"
#include <math.h>
#include <stdio.h>

static int almost_equal(float a, float b, float eps) {
	float d = a - b;
	if (d < 0.0f) d = -d;
	return d <= eps;
}

static int build_remaining_cards(uint64_t used, uint64_t out[52]) {
	int n = 0;
	int r, s;
	for (r = 0; r < 13; r++) {
		for (s = 0; s < 4; s++) {
			uint64_t card = range_make_card(r, s);
			if ((used & card) == 0)
				out[n++] = card;
		}
	}
	return n;
}

static float payoff_from_strengths(int a, int b, float pot, float traverser_contrib) {
	if (a > b) return pot - traverser_contrib;
	if (a < b) return -traverser_contrib;
	return (pot * 0.5f) - traverser_contrib;
}

static float exact_showdown_ev(const GameState *state, int traverser) {
	const int n_board = __builtin_popcountll(state->board);
	const int missing = 5 - n_board;
	const uint64_t traverser_hand = (traverser == P1) ? state->p1_hand : state->p2_hand;
	const uint64_t opponent_hand = (traverser == P1) ? state->p2_hand : state->p1_hand;
	const float traverser_contrib = (traverser == P1) ? state->p1_contribution : state->p2_contribution;
	uint64_t remaining[52];
	uint64_t used = state->board | state->p1_hand | state->p2_hand;
	int n = build_remaining_cards(used, remaining);
	float total = 0.0f;
	int outcomes = 0;

	if (missing == 0) {
		int ts = evaluate(traverser_hand, state->board);
		int os = evaluate(opponent_hand, state->board);
		return payoff_from_strengths(ts, os, state->pot, traverser_contrib);
	}
	if (missing == 1) {
		for (int i = 0; i < n; i++) {
			uint64_t b = state->board | remaining[i];
			int ts = evaluate(traverser_hand, b);
			int os = evaluate(opponent_hand, b);
			total += payoff_from_strengths(ts, os, state->pot, traverser_contrib);
			outcomes++;
		}
	} else if (missing == 2) {
		for (int i = 0; i < n; i++) {
			for (int j = i + 1; j < n; j++) {
				uint64_t b = state->board | remaining[i] | remaining[j];
				int ts = evaluate(traverser_hand, b);
				int os = evaluate(opponent_hand, b);
				total += payoff_from_strengths(ts, os, state->pot, traverser_contrib);
				outcomes++;
			}
		}
	}
	return (outcomes > 0) ? (total / (float)outcomes) : 0.0f;
}

static int test_fold_payouts(void) {
	GameState s;
	gto_init_flop_state(&s, 0, 0, 0);
	s.is_terminal = true;
	s.facing_bet = true;
	s.last_action = ACTION_FOLD;
	s.active_player = P1; /* P1 folded. */
	s.pot = 18.0f;
	s.p1_contribution = 7.0f;
	s.p2_contribution = 11.0f;

	if (!almost_equal(gto_get_payout(&s, P1), -7.0f, 1e-6f)) return 0;
	if (!almost_equal(gto_get_payout(&s, P2), 7.0f, 1e-6f)) return 0;
	return 1;
}

static int test_full_board_showdown(void) {
	GameState s;
	int nc = 0;
	gto_init_flop_state(&s, range_parse_card("As") | range_parse_card("Ah"),
		range_parse_card("Kc") | range_parse_card("Qd"),
		range_parse_board("Kd7h2s3c4d", &nc));
	s.is_terminal = true;
	s.facing_bet = false;
	s.pot = 12.0f;
	s.p1_contribution = 3.0f;
	s.p2_contribution = 9.0f;

	if (nc != 5) return 0;
	if (!almost_equal(gto_get_payout(&s, P1), 9.0f, 1e-6f)) return 0;
	if (!almost_equal(gto_get_payout(&s, P2), -9.0f, 1e-6f)) return 0;
	return 1;
}

static int test_exact_flop_enumeration(void) {
	GameState s;
	int nc = 0;
	float expected_p1, expected_p2;
	gto_init_flop_state(&s, range_parse_card("As") | range_parse_card("Ah"),
		range_parse_card("Kc") | range_parse_card("Qd"),
		range_parse_board("Kd7h2s", &nc));
	s.is_terminal = true;
	s.facing_bet = false;
	s.pot = 12.0f;
	s.p1_contribution = 3.0f;
	s.p2_contribution = 9.0f;

	if (nc != 3) return 0;
	expected_p1 = exact_showdown_ev(&s, P1);
	expected_p2 = exact_showdown_ev(&s, P2);
	if (!almost_equal(gto_get_payout(&s, P1), expected_p1, 1e-6f)) return 0;
	if (!almost_equal(gto_get_payout(&s, P2), expected_p2, 1e-6f)) return 0;
	return 1;
}

static int test_exact_turn_enumeration(void) {
	GameState s;
	int nc = 0;
	float expected;
	gto_init_flop_state(&s, range_parse_card("As") | range_parse_card("Ah"),
		range_parse_card("Kc") | range_parse_card("Qd"),
		range_parse_board("Kd7h2s3c", &nc));
	s.is_terminal = true;
	s.facing_bet = false;
	s.pot = 12.0f;
	s.p1_contribution = 3.0f;
	s.p2_contribution = 9.0f;

	if (nc != 4) return 0;
	expected = exact_showdown_ev(&s, P1);
	return almost_equal(gto_get_payout(&s, P1), expected, 1e-6f);
}

int main(void) {
	int ok = 1;
	init_rank_map();
	init_flush_map();

	ok &= test_fold_payouts();
	ok &= test_full_board_showdown();
	ok &= test_exact_flop_enumeration();
	ok &= test_exact_turn_enumeration();

	if (!ok) {
		printf("[!] solver_payout_test failed\n");
		return 1;
	}
	printf("solver_payout_test passed\n");
	return 0;
}
