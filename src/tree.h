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
} PublicNode; //sizeof(48)

typedef struct {
	uint64_t board;

	uint16_t pot;
	uint16_t p1_stack;
	uint16_t p2_stack;

	//chips put in by respective player
	uint16_t p1_commit;
	uint16_t p2_commit; 
	//cumulative chips invested in the pot, including prior streets
	uint16_t p1_invested;
	uint16_t p2_invested;

	uint8_t active_player; // 0 = P1 OOP (BB), 1 = P2 IP (BTN)
	uint8_t street; //0 flop, 1 turn, 2 river
	uint8_t raises_this_street; //cap betting, 2 raises per street
	uint8_t num_actions_this_street;
	uint8_t last_action_was_fold;
} GameState; //sizeof(32)

PublicNode* build_tree(GameState state, int num_combos);
void destroy_tree(PublicNode* node);

#endif //TREE_H
