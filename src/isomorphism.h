#ifndef ISOMORPHISM_H
#define ISOMORPHISM_H

#include <stddef.h>
#include <stdint.h>

#define NUM_CANONICAL_FLOPS 1755

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

/*
 * Suit-isomorphism for flops. Cards are suit*13+rank (rank 0=2 .. 12=A,
 * suit 0=s, 1=h, 2=d, 3=c), matching walk_tree / get_mask_for_combo.
 *
 * canonicalize_flop_cards maps a 3-card flop to the unique representative
 * of its suit orbit (lex-min suit assignment). If hole_in is set, residual
 * suit symmetry is broken by also lex-minimizing the two hole cards.
 * perm_out[old_suit] = new_suit when provided.
 *
 * Returns 1 on success, 0 if the flop is not 3 distinct cards.
 */
int canonicalize_flop_cards(
	const uint8_t flop_in[3],
	uint8_t flop_out[3],
	const uint8_t* hole_in,
	uint8_t* hole_out,
	uint8_t* perm_out
);

uint64_t canonicalize_flop_board(uint64_t board);
int collect_canonical_flops(uint8_t out[][3], int max_flops);
int format_flop_string(const uint8_t flop[3], char* buf, size_t buf_size, int spaced);

#endif // ISOMORPHISM_H
