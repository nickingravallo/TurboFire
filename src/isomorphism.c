#include "isomorphism.h"

#include <stddef.h>

#define NUM_SUITS 4
#define CARDS_PER_SUIT 13
#define MAX_COMBOS 1326

static int combo_permutations[NUM_SUITS][NUM_SUITS][MAX_COMBOS];
static int combo_permutations_ready = 0;

static uint16_t board_suit_ranks(uint64_t board, int suit) {
	return (uint16_t)((board >> (suit * 16)) & 0x1fffULL);
}

static int card_is_on_board(uint64_t board, int card) {
	int rank = card % CARDS_PER_SUIT;
	int suit = card / CARDS_PER_SUIT;

	return (board & (1ULL << (rank + suit * 16))) != 0;
}

static int combo_index(int card_a, int card_b) {
	int first = card_a < card_b ? card_a : card_b;
	int second = card_a < card_b ? card_b : card_a;
	int row_start = first * (103 - first) / 2;

	return row_start + second - first - 1;
}

static int swap_card_suits(int card, int suit_a, int suit_b) {
	int rank = card % CARDS_PER_SUIT;
	int suit = card / CARDS_PER_SUIT;

	if (suit == suit_a)
		suit = suit_b;
	else if (suit == suit_b)
		suit = suit_a;

	return suit * CARDS_PER_SUIT + rank;
}

int collect_isomorphic_runout_cards(uint64_t board, uint8_t* representatives) {
	int count = 0;

	for (int rank = 0; rank < CARDS_PER_SUIT; rank++) {
		for (int suit = 0; suit < NUM_SUITS; suit++) {
			int card = suit * CARDS_PER_SUIT + rank;
			int is_representative = 1;

			if (card_is_on_board(board, card))
				continue;

			for (int earlier_suit = 0; earlier_suit < suit; earlier_suit++) {
				int earlier_card = earlier_suit * CARDS_PER_SUIT + rank;

				if (!card_is_on_board(board, earlier_card) &&
					board_suit_ranks(board, earlier_suit) == board_suit_ranks(board, suit)) {
					is_representative = 0;
					break;
				}
			}

			if (is_representative && representatives)
				representatives[count] = (uint8_t)card;
			if (is_representative)
				count += 1;
		}
	}

	return count;
}

int collect_isomorphic_card_orbit(
	uint64_t board,
	uint8_t representative,
	uint8_t* cards
) {
	int rank = representative % CARDS_PER_SUIT;
	int representative_suit = representative / CARDS_PER_SUIT;
	uint16_t suit_ranks = board_suit_ranks(board, representative_suit);
	int count = 0;

	if (cards)
		cards[count] = representative;
	count += 1;

	for (int suit = 0; suit < NUM_SUITS; suit++) {
		int card = suit * CARDS_PER_SUIT + rank;

		if (suit == representative_suit || card_is_on_board(board, card))
			continue;
		if (board_suit_ranks(board, suit) != suit_ranks)
			continue;

		if (cards)
			cards[count] = (uint8_t)card;
		count += 1;
	}

	return count;
}

void init_combo_suit_permutations(int num_combos) {
	int combo = 0;

	if (combo_permutations_ready)
		return;
	if (num_combos > MAX_COMBOS)
		num_combos = MAX_COMBOS;

	for (int first = 0; first < 51 && combo < num_combos; first++) {
		for (int second = first + 1; second < 52 && combo < num_combos; second++) {
			for (int suit_a = 0; suit_a < NUM_SUITS; suit_a++) {
				for (int suit_b = 0; suit_b < NUM_SUITS; suit_b++) {
					int mapped_first = swap_card_suits(first, suit_a, suit_b);
					int mapped_second = swap_card_suits(second, suit_a, suit_b);

					combo_permutations[suit_a][suit_b][combo] =
						combo_index(mapped_first, mapped_second);
				}
			}
			combo += 1;
		}
	}

	combo_permutations_ready = 1;
}

int permute_combo_suits(int combo, int suit_a, int suit_b) {
	if (combo < 0 || combo >= MAX_COMBOS)
		return combo;
	if (suit_a < 0 || suit_a >= NUM_SUITS || suit_b < 0 || suit_b >= NUM_SUITS)
		return combo;
	if (!combo_permutations_ready)
		return combo;

	return combo_permutations[suit_a][suit_b][combo];
}
