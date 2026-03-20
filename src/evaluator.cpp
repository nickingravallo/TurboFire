#include "evaluator.h"
#include "omp/HandEvaluator.h"
#include "omp/Hand.h"

//global instance
omp::HandEvaluator eval;

extern "C" void init_evaluator() {
	omp::Hand empty = omp::Hand::empty();
	eval.evaluate(empty);
}


extern "C" int evaluate_board(uint64_t player_hand_mask, uint64_t board_mask) {
	omp::Hand h = omp::Hand::empty();

	uint64_t full_mask = player_hand_mask | board_mask;

	//translate our 16bit space into OMPEVal format
	for (int suit = 0; suit < 4; suit++) {
		int bit_offset = suit * 16;

		uint16_t suit_bits = (full_mask >> bit_offset) & 0x1FFF;

		for (int rank = 0; rank < 13; rank++) {
			if ((suit_bits >> rank) & 1) {
				uint8_t omp_card_idx = (rank * 4) + suit;
				h += omp::Hand(omp_card_idx);
			}
		}
	}

	return eval.evaluate(h);
}
