
#define FOLD  0
#define CALL  1
#define CHECK 1
#define B33   2
#define B52   3
#define B100  4

#include <stdint.h>

int get_legal_actions(GameState* state, int* out_actions) {

}

void dcfr(PublicNode* node) {
	if (node->type == NODE_ACTION) {
		uint8_t legal_actions[8]; //8b align 
		int action_count = get_legal_actions(state, legal_actions);
	}
}
