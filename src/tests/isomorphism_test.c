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

	{
		uint8_t flops[NUM_CANONICAL_FLOPS][3];
		int n = collect_canonical_flops(flops, NUM_CANONICAL_FLOPS);
		uint8_t again[3];

		assert(n == NUM_CANONICAL_FLOPS);
		for (int i = 0; i < n; i++) {
			assert(canonicalize_flop_cards(flops[i], again, NULL, NULL, NULL));
			assert(again[0] == flops[i][0] && again[1] == flops[i][1] &&
				again[2] == flops[i][2]);
		}
	}

	for (int a = 0; a < 50; a++) {
		for (int b = a + 1; b < 51; b++) {
			for (int c = b + 1; c < 52; c++) {
				uint8_t flop[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c};
				uint8_t canon[3];
				uint64_t board =
					card_mask(a % 13, a / 13) |
					card_mask(b % 13, b / 13) |
					card_mask(c % 13, c / 13);

				assert(canonicalize_flop_cards(flop, canon, NULL, NULL, NULL));
				assert(canonicalize_flop_board(board) ==
					(card_mask(canon[0] % 13, canon[0] / 13) |
					 card_mask(canon[1] % 13, canon[1] / 13) |
					 card_mask(canon[2] % 13, canon[2] / 13)));

				for (int sa = 0; sa < 4; sa++) {
					for (int sb = 0; sb < 4; sb++) {
						int d;
						int perm[4];
						uint8_t mapped[3];
						uint8_t mapped_canon[3];

						if (sb == sa)
							continue;
						for (int sc = 0; sc < 4; sc++) {
							if (sc == sa || sc == sb)
								continue;
							d = 6 - sa - sb - sc;
							perm[0] = sa;
							perm[1] = sb;
							perm[2] = sc;
							perm[3] = d;
							for (int i = 0; i < 3; i++)
								mapped[i] = (uint8_t)(perm[flop[i] / 13] * 13 + flop[i] % 13);
							assert(canonicalize_flop_cards(
								mapped, mapped_canon, NULL, NULL, NULL
							));
							assert(mapped_canon[0] == canon[0] &&
								mapped_canon[1] == canon[1] &&
								mapped_canon[2] == canon[2]);
						}
					}
				}
			}
		}
	}

	{
		uint8_t boards[][3] = {
			{12, 24, 28}, /* As Kh 4d  — rainbow */
			{12, 11, 0},  /* As Ks 2s  — monotone */
			{12, 11, 13}, /* As Ks 2h  — two-tone */
		};

		for (unsigned bi = 0; bi < sizeof(boards) / sizeof(boards[0]); bi++) {
			uint8_t flop[3] = {boards[bi][0], boards[bi][1], boards[bi][2]};
			int on_flop[52] = {0};

			on_flop[flop[0]] = on_flop[flop[1]] = on_flop[flop[2]] = 1;
			for (int h1 = 0; h1 < 51; h1++) {
				if (on_flop[h1])
					continue;
				for (int h2 = h1 + 1; h2 < 52; h2++) {
					uint8_t hole[2] = {(uint8_t)h1, (uint8_t)h2};
					uint8_t canon_flop[3];
					uint8_t canon_hole[2];

					if (on_flop[h2])
						continue;
					assert(canonicalize_flop_cards(
						flop, canon_flop, hole, canon_hole, NULL
					));

					for (int sa = 0; sa < 4; sa++) {
						for (int sb = 0; sb < 4; sb++) {
							int d;
							int perm[4];

							if (sb == sa)
								continue;
							for (int sc = 0; sc < 4; sc++) {
								uint8_t mapped_flop[3];
								uint8_t mapped_hole[2];
								uint8_t out_flop[3];
								uint8_t out_hole[2];

								if (sc == sa || sc == sb)
									continue;
								d = 6 - sa - sb - sc;
								perm[0] = sa;
								perm[1] = sb;
								perm[2] = sc;
								perm[3] = d;
								for (int i = 0; i < 3; i++)
									mapped_flop[i] =
										(uint8_t)(perm[flop[i] / 13] * 13 + flop[i] % 13);
								for (int i = 0; i < 2; i++)
									mapped_hole[i] =
										(uint8_t)(perm[hole[i] / 13] * 13 + hole[i] % 13);
								assert(canonicalize_flop_cards(
									mapped_flop, out_flop, mapped_hole, out_hole, NULL
								));
								assert(out_flop[0] == canon_flop[0] &&
									out_flop[1] == canon_flop[1] &&
									out_flop[2] == canon_flop[2]);
								assert(out_hole[0] == canon_hole[0] &&
									out_hole[1] == canon_hole[1]);
							}
						}
					}
				}
			}
		}
	}

	printf("isomorphism tests passed\n");
	return 0;
}
