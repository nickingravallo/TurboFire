#ifndef PARSE_H
#define PARSE_H

#include <stdint.h>

uint64_t parse_board_string(const char* board_str);
uint64_t get_mask_for_combo(int combo_idx); 
int init_range_from_file(const char* path, uint64_t board, int num_combos, float* range);

#endif
