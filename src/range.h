#ifndef RANGE_H
#define RANGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define GRID_SIZE 13
#define RANKS_STR "23456789TJQKA"

/* Hand grid: row 0 = A, row 12 = 2. Pairs on diagonal, suited above, offsuit below. */

/* Return hand string (e.g. "AA", "AKs", "AKo") for grid cell (row, col). */
void hand_at(int row, int col, char *out, size_t out_sz);

/* Return (row, col) for hand string; return -1,-1 if invalid. */
void row_col_for_hand(const char *hand, int *out_row, int *out_col);

/* Card bitmask: rank 0-12 (2-A), suit 0-3 (S,H,D,C). Compatible with ranks.c. */
uint64_t range_make_card(int rank, int suit);

/* Parse card from string e.g. "Ah", "Ks" (2 chars). Return 0 on invalid. */
uint64_t range_parse_card(const char *str);

/* Parse board from string e.g. "AhKhQd" (2 chars per card). Return bitmask; *ncards set. */
uint64_t range_parse_board(const char *str, int *ncards);

/* Generate up to max_combos (c1,c2) bitmask pairs for hand string; board = dead cards.
 * Return number of combos written. Hand must not overlap board. */
int hand_string_to_combos(const char *hand, uint64_t board_dead, uint64_t *out_c1, uint64_t *out_c2, int max_combos);

/* Load range from JSON path. Fills grid[13][13] with weights in [0,1]. Return 0 on success. */
int load_range_json(const char *path, float grid[GRID_SIZE][GRID_SIZE]);

/* Total combos for hand type (pair=6, suited=4, offsuit=12). */
int hand_combos_count(const char *hand);

#endif
