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

