#include "parse.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HAND_TYPES 3
#define HAND_PAIR 0
#define HAND_SUITED 1
#define HAND_OFFSUIT 2

static inline int char_to_rank(char c) {
	c = (char)toupper((unsigned char)c);
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

static int read_file_text(const char* path, char** out_text) {
	FILE* file = fopen(path, "rb");
	long size;
	char* text;

	*out_text = NULL;
	if (!file)
		return 0;

	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return 0;
	}

	size = ftell(file);
	if (size < 0) {
		fclose(file);
		return 0;
	}
	rewind(file);

	text = (char*)malloc((size_t)size + 1);
	if (!text) {
		fclose(file);
		return 0;
	}

	if (fread(text, 1, (size_t)size, file) != (size_t)size) {
		free(text);
		fclose(file);
		return 0;
	}

	text[size] = '\0';
	fclose(file);
	*out_text = text;
	return 1;
}

static const char* find_object_end(const char* open_brace) {
	int depth = 0;
	int in_string = 0;
	int escaped = 0;

	for (const char* p = open_brace; *p; p++) {
		if (in_string) {
			if (escaped) {
				escaped = 0;
			} else if (*p == '\\') {
				escaped = 1;
			} else if (*p == '"') {
				in_string = 0;
			}
			continue;
		}

		if (*p == '"') {
			in_string = 1;
		} else if (*p == '{') {
			depth++;
		} else if (*p == '}') {
			depth--;
			if (depth == 0)
				return p;
		}
	}

	return NULL;
}

static int parse_hand_key(const char* key, int* high_rank, int* low_rank, int* hand_type) {
	int r1;
	int r2;
	size_t len = strlen(key);

	if (len < 2 || len > 3)
		return 0;

	r1 = char_to_rank(key[0]);
	r2 = char_to_rank(key[1]);
	if (r1 < 0 || r2 < 0)
		return 0;

	if (r1 == r2) {
		if (len != 2)
			return 0;
		*high_rank = r1;
		*low_rank = r2;
		*hand_type = HAND_PAIR;
		return 1;
	}

	if (len != 3)
		return 0;

	*high_rank = r1 > r2 ? r1 : r2;
	*low_rank = r1 > r2 ? r2 : r1;

	if (key[2] == 's' || key[2] == 'S') {
		*hand_type = HAND_SUITED;
		return 1;
	}

	if (key[2] == 'o' || key[2] == 'O') {
		*hand_type = HAND_OFFSUIT;
		return 1;
	}

	return 0;
}

static void cards_for_combo(int combo_idx, int* c1, int* c2) {
	int first = (int)floor((103.0 - sqrt(10609.0 - 8.0 * combo_idx)) / 2.0);
	int row_start = first * (103 - first) / 2;
	int offset = combo_idx - row_start;

	*c1 = first;
	*c2 = first + 1 + offset;
}

static int parse_hands_object(const char* text, float hand_weights[13][13][HAND_TYPES]) {
	const char* hands = strstr(text, "\"hands\"");
	const char* open_brace;
	const char* close_brace;
	const char* p;
	int parsed_hands = 0;

	if (!hands)
		return 0;

	open_brace = strchr(hands, '{');
	if (!open_brace)
		return 0;

	close_brace = find_object_end(open_brace);
	if (!close_brace)
		return 0;

	p = open_brace + 1;
	while (p < close_brace) {
		char key[4] = {0};
		const char* key_start;
		const char* key_end;
		char* value_end;
		float weight;
		int high_rank;
		int low_rank;
		int hand_type;
		size_t key_len;

		while (p < close_brace && *p != '"')
			p++;
		if (p >= close_brace)
			break;

		key_start = p + 1;
		key_end = strchr(key_start, '"');
		if (!key_end || key_end > close_brace)
			return 0;

		key_len = (size_t)(key_end - key_start);
		if (key_len >= sizeof(key))
			return 0;
		memcpy(key, key_start, key_len);
		key[key_len] = '\0';

		p = key_end + 1;
		while (p < close_brace && isspace((unsigned char)*p))
			p++;
		if (p >= close_brace || *p != ':')
			return 0;
		p++;
		while (p < close_brace && isspace((unsigned char)*p))
			p++;

		weight = strtof(p, &value_end);
		if (value_end == p)
			return 0;
		if (weight < 0.0f)
			weight = 0.0f;
		if (weight > 1.0f)
			weight = 1.0f;

		if (!parse_hand_key(key, &high_rank, &low_rank, &hand_type))
			return 0;

		hand_weights[high_rank][low_rank][hand_type] = weight;
		parsed_hands++;
		p = value_end;
	}

	return parsed_hands > 0;
}

int init_range_from_file(const char* path, uint64_t board, int num_combos, float* range) {
	char* text;
	float hand_weights[13][13][HAND_TYPES] = {{{0.0f}}};
	float total_weight = 0.0f;

	if (!path || !range || num_combos <= 0)
		return 0;

	memset(range, 0, (size_t)num_combos * sizeof(float));

	if (!read_file_text(path, &text))
		return 0;

	if (!parse_hands_object(text, hand_weights)) {
		free(text);
		return 0;
	}
	free(text);

	for (int combo = 0; combo < num_combos; combo++) {
		int c1;
		int c2;
		int r1;
		int r2;
		int s1;
		int s2;
		int high_rank;
		int low_rank;
		int hand_type;
		float weight;

		if (get_mask_for_combo(combo) & board)
			continue;

		cards_for_combo(combo, &c1, &c2);
		r1 = c1 % 13;
		r2 = c2 % 13;
		s1 = c1 / 13;
		s2 = c2 / 13;

		if (r1 == r2) {
			high_rank = r1;
			low_rank = r2;
			hand_type = HAND_PAIR;
		} else {
			high_rank = r1 > r2 ? r1 : r2;
			low_rank = r1 > r2 ? r2 : r1;
			hand_type = s1 == s2 ? HAND_SUITED : HAND_OFFSUIT;
		}

		weight = hand_weights[high_rank][low_rank][hand_type];
		if (weight <= 0.0f)
			continue;

		range[combo] = weight;
		total_weight += weight;
	}

	if (total_weight <= 0.0f)
		return 0;

	for (int combo = 0; combo < num_combos; combo++)
		range[combo] /= total_weight;

	return 1;
}
