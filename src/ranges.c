#include "ranges.h"
#include "parse.h"

#include <stdlib.h>

typedef struct {
	int combo;
	int score;
} ComboScore;

static void combo_to_cards(int combo_idx, int* c1, int* c2) {
	uint64_t mask = get_mask_for_combo(combo_idx);
	int found = 0;

	for (int card = 0; card < 52; card++) {
		int rank = card % 13;
		int suit = card / 13;
		uint64_t bit = 1ULL << (rank + suit * 16);

		if (!(mask & bit))
			continue;

		if (found == 0)
			*c1 = card;
		else
			*c2 = card;

		found += 1;
	}
}

static int hand_score(int c1, int c2) {
	int r1 = c1 % 13;
	int r2 = c2 % 13;
	int hi = r1 > r2 ? r1 : r2;
	int lo = r1 > r2 ? r2 : r1;
	int suited = (c1 / 13) == (c2 / 13);
	int gap = hi - lo;

	if (hi == lo)
		return 300 + hi;

	if (suited) {
		int score = 150 + hi * 10 + lo;

		if (gap <= 1)
			score += 8;
		else if (gap <= 3)
			score += 4;

		return score;
	}

	return hi * 10 + lo;
}

static int compare_combo_score(const void* a, const void* b) {
	const ComboScore* left = (const ComboScore*)a;
	const ComboScore* right = (const ComboScore*)b;

	return right->score - left->score;
}

static void init_range_by_percent(uint64_t board, int num_combos, float* range, float keep_pct) {
	ComboScore* ranked = (ComboScore*)malloc((size_t)num_combos * sizeof(ComboScore));
	int live = 0;
	int keep;

	for (int combo = 0; combo < num_combos; combo++) {
		int c1;
		int c2;

		if (get_mask_for_combo(combo) & board)
			continue;

		combo_to_cards(combo, &c1, &c2);
		ranked[live].combo = combo;
		ranked[live].score = hand_score(c1, c2);
		live += 1;
	}

	qsort(ranked, (size_t)live, sizeof(ComboScore), compare_combo_score);

	keep = (int)((float)live * keep_pct + 0.5f);
	if (keep < 1)
		keep = 1;
	if (keep > live)
		keep = live;

	for (int combo = 0; combo < num_combos; combo++)
		range[combo] = 0.0f;

	for (int i = 0; i < keep; i++)
		range[ranked[i].combo] = 1.0f;

	free(ranked);

	for (int combo = 0; combo < num_combos; combo++) {
		if (get_mask_for_combo(combo) & board)
			range[combo] = 0.0f;
	}

	{
		float total = 0.0f;

		for (int combo = 0; combo < num_combos; combo++)
			total += range[combo];

		if (total > 0.0f) {
			for (int combo = 0; combo < num_combos; combo++)
				range[combo] /= total;
		}
	}
}

static float bb_keep_pct(RangeMode mode) {
	switch (mode) {
		case RANGE_TEN_PERCENT:
			return 0.10f;
		case RANGE_CONDENSED:
			return 0.28f;
		case RANGE_WIDE:
		default:
			return 0.88f;
	}
}

static float btn_keep_pct(RangeMode mode) {
	switch (mode) {
		case RANGE_TEN_PERCENT:
			return 0.10f;
		case RANGE_CONDENSED:
			return 0.25f;
		case RANGE_WIDE:
		default:
			return 0.78f;
	}
}

const char* range_mode_label(RangeMode mode) {
	switch (mode) {
		case RANGE_TEN_PERCENT:
			return "top 10% of live combos (both players)";
		case RANGE_CONDENSED:
			return "condensed (BTN ~25%, BB ~28%)";
		case RANGE_WIDE:
		default:
			return "wide (BTN ~78%, BB ~88%)";
	}
}

void init_btn_range(uint64_t board, int num_combos, float* range, RangeMode mode) {
	init_range_by_percent(board, num_combos, range, btn_keep_pct(mode));
}

void init_bb_range(uint64_t board, int num_combos, float* range, RangeMode mode) {
	init_range_by_percent(board, num_combos, range, bb_keep_pct(mode));
}
