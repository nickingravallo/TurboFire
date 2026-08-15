#ifndef RANGES_H
#define RANGES_H

#include <stdint.h>

// P1 = BB (OOP), P2 = BTN (IP) in heads-up postflop
typedef enum {
	RANGE_WIDE = 0,      // BTN ~78%, BB ~88% (default SRP-ish)
	RANGE_CONDENSED = 1, // BTN ~25%, BB ~28% (tight / 3bet-pot-ish)
	RANGE_TEN_PERCENT = 2, // Both players keep the top ~10% of live combos
} RangeMode;

void init_bb_range(uint64_t board, int num_combos, float* range, RangeMode mode);
void init_btn_range(uint64_t board, int num_combos, float* range, RangeMode mode);
const char* range_mode_label(RangeMode mode);

#endif // RANGES_H
