#ifndef PARSE_H
#define PARSE_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t mask;
    float weight;
} HandCombo;

typedef struct {
    HandCombo combos[1326];
    int num_combos;
} PlayerRange;

void parse_json_range(const char* json_string, PlayerRange* out_range);
void apply_card_removal(PlayerRange* range, uint64_t dead_cards);
void print_range_grid(const PlayerRange* range);
uint64_t parse_board_string(const char* board_str);

#endif //PARSE_H
