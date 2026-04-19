#include <stdint.h>
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

// A helper to find a valid 64-bit mask for a given bucket
extern "C" uint64_t get_mask_for_combo(IsoMap* map, int target_bucket) {
    for (int combo = 0; combo < 1326; combo++) {
        if (map->combo_to_bucket[combo] == target_bucket) {
            int c1 = 0, c2 = 0, counter = 0;
            for (int i = 0; i < 51; i++) {
                for (int j = i + 1; j < 52; j++) {
                    if (counter == combo) { c1 = i; c2 = j; break; }
                    counter++;
                }
                if (c1 || c2) break;
            }
            int r1 = c1 % 13; int s1 = c1 / 13;
            int r2 = c2 % 13; int s2 = c2 / 13;
            return (1ULL << (r1 + (s1 * 16))) | (1ULL << (r2 + (s2 * 16)));
        }
    }
    return 0; // Should never reach here if bucket is valid
}
