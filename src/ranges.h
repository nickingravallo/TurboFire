#ifndef RANGES_H
#define RANGES_H

#include <stdint.h>

// P1 = BB (OOP), P2 = BTN (IP) in heads-up postflop
void init_bb_range(uint64_t board, int num_combos, float* range);
void init_btn_range(uint64_t board, int num_combos, float* range);

#endif // RANGES_H
