#include "parse.h"

static inline int char_to_rank(char c) {
	if (c >= '2' && c <= '9') return c - '2';
	if (c == 'T') return 8;
	if (c == 'J') return 9;
	if (c == 'Q') return 10;
	if (c == 'K') return 11;
	if (c == 'A') return 12;
	return -1;
}

static inline int char_to_suit(char c) {
	if (c == 's' || c == 'S') return 0; //spades
	if (c == 'h' || c == 'H') return 1; //hearts
	if (c == 'd' || c == 'D') return 2; //diamonds
	if (c == 'c' || c == 'C') return 3; //clubs
	return -1;
}


uint64_t parse_board_string(const char* board_str) {
	uint64_t board_mask = 0;
	int i = 0;

	while (board_str[i] != '\0') {
		if (board_str[i] == ' ') { //skip spaces if user formatted As Ks 2h
			i++;
			continue;
		}

		int rank = char_to_rank(board_str[i]);
		int suit = char_to_suit(board_str[i+1]);
		if (rank != -1 && suit != -1)
			board_mask |= (1ULL << (rank + (suit * 16)));

		i += 2;
	}

	return board_mask;
}

//WARNING: Generated with AI. Used for creating a mask out of a combo, still testing.
static inline uint64_t card_bit(int card) {
	int rank = card % 13;
	int suit = card / 13;
	return 1ULL << (rank + suit * 16);
}
uint64_t get_mask_for_combo(int combo_idx) {
	if (combo_idx < 0 || combo_idx >= 1326) {
		return 0;
	}
	int c1 = (int)floor((103.0 - sqrt(10609.0 - 8.0 * combo_idx)) / 2.0);
	int row_start = c1 * (103 - c1) / 2;
	int offset = combo_idx - row_start;
	int c2 = c1 + 1 + offset;
	return card_bit(c1) | card_bit(c2);
}
