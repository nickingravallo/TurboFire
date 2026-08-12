#ifndef ISOMORPHISM_H
#define ISOMORPHISM_H

#include <stdint.h>

/*
 * Returns one representative card per orbit under suit permutations that
 * leave the current public board unchanged.
 */
int collect_isomorphic_runout_cards(uint64_t board, uint8_t* representatives);

/*
 * Expands a representative back to every physical card in its orbit.
 * The representative is always the first card returned.
 */
int collect_isomorphic_card_orbit(
	uint64_t board,
	uint8_t representative,
	uint8_t* cards
);

/*
 * Maps a combo index through the transposition of two suits. The combo
 * indexing matches get_mask_for_combo().
 */
void init_combo_suit_permutations(int num_combos);
int permute_combo_suits(int combo, int suit_a, int suit_b);

#endif // ISOMORPHISM_H
