#include "range.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char RANKS[] = "23456789TJQKA";

void hand_at(int row, int col, char *out, size_t out_sz) {
	if (out_sz < 4) return;
	row = row < 0 ? 0 : (row >= GRID_SIZE ? GRID_SIZE - 1 : row);
	col = col < 0 ? 0 : (col >= GRID_SIZE ? GRID_SIZE - 1 : col);
	int r_high = 12 - row;
	int r_low  = 12 - col;
	if (row == col) {
		snprintf(out, out_sz, "%c%c", RANKS[r_high], RANKS[r_high]);
		return;
	}
	if (row < col) {
		snprintf(out, out_sz, "%c%cs", RANKS[r_high], RANKS[r_low]);
		return;
	}
	snprintf(out, out_sz, "%c%co", RANKS[12 - col], RANKS[12 - row]);
}

void row_col_for_hand(const char *hand, int *out_row, int *out_col) {
	if (!hand || !out_row || !out_col) { *out_row = *out_col = -1; return; }
	char h[8];
	size_t i, n = 0;
	for (i = 0; hand[i] && n < sizeof(h) - 1; i++)
		if (!isspace((unsigned char)hand[i])) h[n++] = (char)toupper((unsigned char)hand[i]);
	h[n] = '\0';
	if (n < 2) { *out_row = *out_col = -1; return; }
	int r1 = -1, r2 = -1;
	for (i = 0; i < 13; i++) {
		if (h[0] == RANKS[i]) r1 = (int)i;
		if (h[1] == RANKS[i]) r2 = (int)i;
	}
	if (r1 < 0 || r2 < 0) { *out_row = *out_col = -1; return; }
	int high = r1 > r2 ? r1 : r2;
	int low  = r1 > r2 ? r2 : r1;
	bool suited = false;
	if (n >= 3 && (h[2] == 'S' || h[2] == 's')) suited = true;
	else if (n >= 3 && (h[2] == 'O' || h[2] == 'o')) suited = false;
	else if (high != low) { *out_row = *out_col = -1; return; }
	int row = 12 - high;
	int col = 12 - low;
	if (high == low) {
		*out_row = row;
		*out_col = row;
		return;
	}
	if (suited) {
		*out_row = row;
		*out_col = col;
		return;
	}
	*out_row = col;
	*out_col = row;
}

uint64_t range_make_card(int rank, int suit) {
	if (rank < 0 || rank > 12 || suit < 0 || suit > 3) return 0;
	return 1ULL << (rank + suit * 16);
}

static int rank_char_to_int(char c) {
	for (int i = 0; i < 13; i++)
		if (RANKS[i] == (char)toupper((unsigned char)c)) return i;
	return -1;
}
static int suit_char_to_int(char c) {
	switch ((char)toupper((unsigned char)c)) {
		case 'S': return 0;
		case 'H': return 1;
		case 'D': return 2;
		case 'C': return 3;
		default: return -1;
	}
}

uint64_t range_parse_card(const char *str) {
	if (!str || !str[0] || !str[1]) return 0;
	int r = rank_char_to_int(str[0]);
	int s = suit_char_to_int(str[1]);
	if (r < 0 || s < 0) return 0;
	return range_make_card(r, s);
}

uint64_t range_parse_board(const char *str, int *ncards) {
	uint64_t board = 0;
	int n = 0;
	if (ncards) *ncards = 0;
	if (!str) return 0;
	while (str[0] && str[1] && n < 5) {
		uint64_t c = range_parse_card(str);
		if (!c || (board & c)) return board;
		board |= c;
		n++;
		str += 2;
	}
	if (ncards) *ncards = n;
	return board;
}

int range_parse_board_cards(const char *str, uint64_t out_cards[5], int *ncards) {
	uint64_t seen = 0;
	int n = 0;
	if (ncards) *ncards = 0;
	if (!str || !out_cards)
		return -1;
	while (str[0] && str[1]) {
		uint64_t card;
		if (n >= 5)
			return -1;
		card = range_parse_card(str);
		if (!card || (seen & card))
			return -1;
		out_cards[n++] = card;
		seen |= card;
		str += 2;
	}
	if (str[0] != '\0')
		return -1;
	if (ncards) *ncards = n;
	return 0;
}

int hand_combos_count(const char *hand) {
	int row, col;
	row_col_for_hand(hand, &row, &col);
	if (row < 0) return 0;
	if (row == col) return 6;   /* pair */
	if (row < col) return 4;    /* suited */
	return 12;                  /* offsuit */
}

int hand_string_to_combos(const char *hand, uint64_t board_dead, uint64_t *out_c1, uint64_t *out_c2, int max_combos) {
	int row, col;
	row_col_for_hand(hand, &row, &col);
	if (row < 0 || !out_c1 || !out_c2 || max_combos <= 0) return 0;
	int r_high = 12 - row;
	int r_low  = 12 - col;
	if (row == col) {
		/* pair: 6 combos (suit pairs) */
		int n = 0;
		for (int s1 = 0; s1 < 4 && n < max_combos; s1++) {
			for (int s2 = s1 + 1; s2 < 4 && n < max_combos; s2++) {
				uint64_t c1 = range_make_card(r_high, s1);
				uint64_t c2 = range_make_card(r_high, s2);
				if ((c1 & board_dead) || (c2 & board_dead)) continue;
				out_c1[n] = c1;
				out_c2[n] = c2;
				n++;
			}
		}
		return n;
	}
	if (row < col) {
		/* suited */
		int n = 0;
		for (int s = 0; s < 4 && n < max_combos; s++) {
			uint64_t c1 = range_make_card(r_high, s);
			uint64_t c2 = range_make_card(r_low, s);
			if ((c1 & board_dead) || (c2 & board_dead) || (c1 & c2)) continue;
			out_c1[n] = c1;
			out_c2[n] = c2;
			n++;
		}
		return n;
	}
	/* offsuit */
	int n = 0;
	for (int s1 = 0; s1 < 4 && n < max_combos; s1++) {
		for (int s2 = 0; s2 < 4 && n < max_combos; s2++) {
			if (s1 == s2) continue;
			uint64_t c1 = range_make_card(r_high, s1);
			uint64_t c2 = range_make_card(r_low, s2);
			if ((c1 & board_dead) || (c2 & board_dead) || (c1 & c2)) continue;
			out_c1[n] = c1;
			out_c2[n] = c2;
			n++;
		}
	}
	return n;
}

/* Minimal JSON: find "hands": { "AA": 1.0, ... } and parse keys/values. */
int load_range_json(const char *path, float grid[GRID_SIZE][GRID_SIZE]) {
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = (char *)malloc((size_t)len + 1);
	if (!buf) { fclose(f); return -1; }
	size_t nread = fread(buf, 1, (size_t)len, f);
	fclose(f);
	buf[nread] = '\0';

	for (int r = 0; r < GRID_SIZE; r++)
		for (int c = 0; c < GRID_SIZE; c++)
			grid[r][c] = 0.0f;

	/* Find "hands" then "{" then iterate "key": value */
	char *p = strstr(buf, "\"hands\"");
	if (!p) { free(buf); return -1; }
	p = strchr(p, '{');
	if (!p) { free(buf); return -1; }
	p++;

	while (*p) {
		while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == ',')) p++;
		if (*p != '"') break;
		p++;
		char key[16];
		size_t k = 0;
		while (*p && *p != '"' && k < sizeof(key) - 1) key[k++] = *p++;
		key[k] = '\0';
		if (*p == '"') p++;
		while (*p && *p != ':') p++;
		if (*p == ':') p++;
		float val = 0.0f;
		sscanf(p, "%f", &val);
		if (val < 0.0f) val = 0.0f;
		if (val > 1.0f) val = 1.0f;
		int row, col;
		row_col_for_hand(key, &row, &col);
		if (row >= 0 && row < GRID_SIZE && col >= 0 && col < GRID_SIZE)
			grid[row][col] = val;
		while (*p && *p != ',' && *p != '}') p++;
		if (*p == '}') break;
	}
	free(buf);
	return 0;
}
