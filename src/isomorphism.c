#include "isomorphism.h"

#include <stddef.h>
#include <string.h>

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

typedef struct {
	uint8_t rank;
	uint8_t suit;
} IsoCard;

static const char ISO_RANK_CHARS[] = "23456789TJQKA";
static const char ISO_SUIT_CHARS[] = "shdc";

static void sort_iso_key(IsoCard* cards, int n) {
	for (int i = 0; i < n; i++) {
		for (int j = i + 1; j < n; j++) {
			if (cards[j].rank > cards[i].rank ||
				(cards[j].rank == cards[i].rank && cards[j].suit < cards[i].suit)) {
				IsoCard tmp = cards[i];
				cards[i] = cards[j];
				cards[j] = tmp;
			}
		}
	}
}

static int cmp_iso_key(const IsoCard* a, const IsoCard* b, int n) {
	for (int i = 0; i < n; i++) {
		if (a[i].rank != b[i].rank)
			return (int)a[i].rank - (int)b[i].rank;
		if (a[i].suit != b[i].suit)
			return (int)a[i].suit - (int)b[i].suit;
	}
	return 0;
}

static void sort_card_ids_display(uint8_t* cards, int n) {
	for (int i = 0; i < n; i++) {
		for (int j = i + 1; j < n; j++) {
			int ri = cards[i] % CARDS_PER_SUIT;
			int rj = cards[j] % CARDS_PER_SUIT;

			if (rj > ri || (rj == ri && cards[j] > cards[i])) {
				uint8_t tmp = cards[i];
				cards[i] = cards[j];
				cards[j] = tmp;
			}
		}
	}
}

static IsoCard map_iso_card(uint8_t card, const int perm[NUM_SUITS]) {
	IsoCard mapped;

	mapped.rank = (uint8_t)(card % CARDS_PER_SUIT);
	mapped.suit = (uint8_t)perm[card / CARDS_PER_SUIT];
	return mapped;
}

static uint8_t pack_iso_card(IsoCard card) {
	return (uint8_t)(card.suit * CARDS_PER_SUIT + card.rank);
}

int canonicalize_flop_cards(
	const uint8_t flop_in[3],
	uint8_t flop_out[3],
	const uint8_t* hole_in,
	uint8_t* hole_out,
	uint8_t* perm_out
) {
	int best_perm[NUM_SUITS] = {0, 1, 2, 3};
	IsoCard best_flop[3];
	IsoCard best_hole[2];
	int have_best = 0;
	int have_holes = hole_in != NULL;

	if (!flop_in || !flop_out)
		return 0;
	if (flop_in[0] == flop_in[1] || flop_in[0] == flop_in[2] || flop_in[1] == flop_in[2])
		return 0;
	if (flop_in[0] > 51 || flop_in[1] > 51 || flop_in[2] > 51)
		return 0;
	if (have_holes && (hole_in[0] > 51 || hole_in[1] > 51 || hole_in[0] == hole_in[1]))
		return 0;

	for (int a = 0; a < NUM_SUITS; a++) {
		for (int b = 0; b < NUM_SUITS; b++) {
			int d;
			int perm[NUM_SUITS];

			if (b == a)
				continue;
			for (int c = 0; c < NUM_SUITS; c++) {
				IsoCard flop[3];
				IsoCard hole[2];
				int better;

				if (c == a || c == b)
					continue;
				d = 6 - a - b - c;
				perm[0] = a;
				perm[1] = b;
				perm[2] = c;
				perm[3] = d;

				flop[0] = map_iso_card(flop_in[0], perm);
				flop[1] = map_iso_card(flop_in[1], perm);
				flop[2] = map_iso_card(flop_in[2], perm);
				sort_iso_key(flop, 3);

				if (have_holes) {
					hole[0] = map_iso_card(hole_in[0], perm);
					hole[1] = map_iso_card(hole_in[1], perm);
					sort_iso_key(hole, 2);
				}

				if (!have_best)
					better = 1;
				else {
					int flop_cmp = cmp_iso_key(flop, best_flop, 3);

					if (flop_cmp < 0)
						better = 1;
					else if (flop_cmp > 0)
						better = 0;
					else if (have_holes)
						better = cmp_iso_key(hole, best_hole, 2) < 0;
					else
						better = 0;
				}

				if (!better)
					continue;

				have_best = 1;
				memcpy(best_flop, flop, sizeof(best_flop));
				if (have_holes)
					memcpy(best_hole, hole, sizeof(best_hole));
				memcpy(best_perm, perm, sizeof(best_perm));
			}
		}
	}

	if (!have_best)
		return 0;

	flop_out[0] = pack_iso_card(map_iso_card(flop_in[0], best_perm));
	flop_out[1] = pack_iso_card(map_iso_card(flop_in[1], best_perm));
	flop_out[2] = pack_iso_card(map_iso_card(flop_in[2], best_perm));
	sort_card_ids_display(flop_out, 3);

	if (have_holes && hole_out) {
		hole_out[0] = pack_iso_card(map_iso_card(hole_in[0], best_perm));
		hole_out[1] = pack_iso_card(map_iso_card(hole_in[1], best_perm));
		sort_card_ids_display(hole_out, 2);
	}
	if (perm_out) {
		for (int suit = 0; suit < NUM_SUITS; suit++)
			perm_out[suit] = (uint8_t)best_perm[suit];
	}
	return 1;
}

static int board_to_flop_cards(uint64_t board, uint8_t flop[3]) {
	int n = 0;

	for (int card = 0; card < 52 && n < 3; card++) {
		if (card_is_on_board(board, card))
			flop[n++] = (uint8_t)card;
	}
	if (n != 3)
		return 0;
	if (__builtin_popcountll(board) != 3)
		return 0;
	return 1;
}

uint64_t canonicalize_flop_board(uint64_t board) {
	uint8_t flop_in[3];
	uint8_t flop_out[3];
	uint64_t out = 0;

	if (!board_to_flop_cards(board, flop_in))
		return board;
	if (!canonicalize_flop_cards(flop_in, flop_out, NULL, NULL, NULL))
		return board;

	for (int i = 0; i < 3; i++) {
		int rank = flop_out[i] % CARDS_PER_SUIT;
		int suit = flop_out[i] / CARDS_PER_SUIT;

		out |= 1ULL << (rank + suit * 16);
	}
	return out;
}

int collect_canonical_flops(uint8_t out[][3], int max_flops) {
	int count = 0;

	for (int a = 0; a < 50; a++) {
		for (int b = a + 1; b < 51; b++) {
			for (int c = b + 1; c < 52; c++) {
				uint8_t flop_in[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c};
				uint8_t canon[3];
				uint8_t sorted_in[3];

				if (!canonicalize_flop_cards(flop_in, canon, NULL, NULL, NULL))
					continue;

				sorted_in[0] = flop_in[0];
				sorted_in[1] = flop_in[1];
				sorted_in[2] = flop_in[2];
				sort_card_ids_display(sorted_in, 3);
				if (sorted_in[0] != canon[0] || sorted_in[1] != canon[1] ||
					sorted_in[2] != canon[2])
					continue;

				if (out && count < max_flops) {
					out[count][0] = canon[0];
					out[count][1] = canon[1];
					out[count][2] = canon[2];
				}
				count += 1;
			}
		}
	}

	return count;
}

int format_flop_string(const uint8_t flop[3], char* buf, size_t buf_size, int spaced) {
	uint8_t cards[3];
	size_t n = 0;

	if (!flop || !buf || buf_size == 0)
		return 0;

	cards[0] = flop[0];
	cards[1] = flop[1];
	cards[2] = flop[2];
	sort_card_ids_display(cards, 3);

	for (int i = 0; i < 3; i++) {
		int rank = cards[i] % CARDS_PER_SUIT;
		int suit = cards[i] / CARDS_PER_SUIT;

		if (spaced && i > 0) {
			if (n + 1 >= buf_size)
				break;
			buf[n++] = ' ';
		}
		if (n + 2 >= buf_size)
			break;
		buf[n++] = ISO_RANK_CHARS[rank];
		buf[n++] = ISO_SUIT_CHARS[suit];
	}
	if (n < buf_size)
		buf[n] = '\0';
	else
		buf[buf_size - 1] = '\0';
	return (int)n;
}
