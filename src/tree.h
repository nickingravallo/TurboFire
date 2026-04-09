#ifndef TREE_H
#define TREE_H

#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>

typedef enum {
	NODE_ACTION,
	NODE_CHANCE,
	NODE_TERMINAL
} NodeType;
 
typedef struct PublicNode {
	NodeType type;
	uint8_t active_player;
	uint8_t num_children;
	uint8_t* dealt_cards;
	
	struct PublicNode** children;

	//for action nodes only
	float* regret_sum;
	float* strategy_sum;

	//for chance nodes
	float* chance_weights;
} PublicNode; //sizeof(48)

typedef struct {
	uint64_t board;

	uint16_t pot;
	uint16_t p1_stack;
	uint16_t p2_stack;

	//chips put in by respective player
	uint16_t p1_commit;
	uint16_t p2_commit; 

	uint8_t active_player; //0 = P1 (OOP), 1 P2 (IP)
	uint8_t street; //0 flop, 1 turn, 2 river
	uint8_t raises_this_street; //cap betting, 3 per street likely
	uint8_t num_actions_this_street;
	uint8_t last_action_was_fold;
} GameState; //sizeof(24)

#endif //TREE_H
