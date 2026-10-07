#include "evaluator.hpp"
#include "omp/HandEvaluator.h"
#include "omp/Hand.h"
//you wont find omp here, going to remove it from future refs. search "ompeval" and place it in the correct directory

//global instance
omp::HandEvaluator eval;

void init_evaluator() {
	omp::Hand empty = omp::Hand::empty();
	eval.evaluate(empty);
}

//evaluate_board calls eval.evaluate() where higher is better
int evaluate_board(std::uint64_t player_hand_mask, std::uint64_t board_mask) {
	omp::Hand h = omp::Hand::empty();

	std::uint64_t full_mask = player_hand_mask | board_mask;

	//translate our 16bit space into OMPEVal format
	for (int suit = 0; suit < 4; suit++) {
		int bit_offset = suit * 16;

		std::uint16_t suit_bits = (full_mask >> bit_offset) & 0x1FFF;

		for (int rank = 0; rank < 13; rank++) {
			if ((suit_bits >> rank) & 1) {
				std::uint8_t omp_card_idx = (rank * 4) + suit;
				h += omp::Hand(omp_card_idx);
			}
		}
	}

	return eval.evaluate(h);
}
