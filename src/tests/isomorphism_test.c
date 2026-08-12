#include "../isomorphism.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define NUM_COMBOS 1326

static uint64_t card_mask(int rank, int suit) {
	return 1ULL << (rank + suit * 16);
}

static int orbit_card_count(uint64_t board) {
	uint8_t representatives[52];
	int representative_count = collect_isomorphic_runout_cards(board, representatives);
	int total = 0;

	for (int i = 0; i < representative_count; i++) {
		uint8_t cards[4];
		total += collect_isomorphic_card_orbit(board, representatives[i], cards);
	}

	return total;
}

static void assert_runout_partition(uint64_t board) {
	uint8_t representatives[52];
	int representative_count = collect_isomorphic_runout_cards(board, representatives);
	uint8_t seen[52] = {0};

	for (int i = 0; i < representative_count; i++) {
		uint8_t cards[4];
		int orbit_size = collect_isomorphic_card_orbit(board, representatives[i], cards);

		assert(cards[0] == representatives[i]);
		for (int j = 0; j < orbit_size; j++) {
			assert(!seen[cards[j]]);
			seen[cards[j]] = 1;
		}
	}

	for (int card = 0; card < 52; card++) {
		int rank = card % 13;
		int suit = card / 13;
		int on_board = (board & card_mask(rank, suit)) != 0;

		assert(seen[card] == !on_board);
	}
}

int main(void) {
	uint64_t rainbow = card_mask(12, 0) | card_mask(11, 2) | card_mask(2, 1);
	uint64_t monotone = card_mask(12, 0) | card_mask(11, 0) | card_mask(2, 0);
	uint64_t two_tone = card_mask(12, 0) | card_mask(11, 0) | card_mask(2, 1);

	assert(collect_isomorphic_runout_cards(rainbow, NULL) == 49);
	assert(collect_isomorphic_runout_cards(monotone, NULL) == 23);
	assert(collect_isomorphic_runout_cards(two_tone, NULL) == 36);

	assert(orbit_card_count(rainbow) == 49);
	assert(orbit_card_count(monotone) == 49);
	assert(orbit_card_count(two_tone) == 49);

	for (int first = 0; first < 50; first++) {
		for (int second = first + 1; second < 51; second++) {
			for (int third = second + 1; third < 52; third++) {
				uint64_t board =
					card_mask(first % 13, first / 13) |
					card_mask(second % 13, second / 13) |
					card_mask(third % 13, third / 13);

				assert_runout_partition(board);
			}
		}
	}

	init_combo_suit_permutations(NUM_COMBOS);
	for (int suit_a = 0; suit_a < 4; suit_a++) {
		for (int suit_b = 0; suit_b < 4; suit_b++) {
			for (int combo = 0; combo < NUM_COMBOS; combo++) {
				int mapped = permute_combo_suits(combo, suit_a, suit_b);
				assert(mapped >= 0 && mapped < NUM_COMBOS);
				assert(permute_combo_suits(mapped, suit_a, suit_b) == combo);
			}
		}
	}

	printf("isomorphism tests passed\n");
	return 0;
}
