#ifndef EVALUATOR_H
#define EVALUATOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void init_evaluator();

int evaluate_board(uint64_t player_hand_mask, uint64_t board_mask);

#ifdef __cplusplus
}
#endif

#endif // EVALUATOR_H
