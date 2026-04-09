
#define FOLD  -1
#define PASS  0
#define B10   1
#define B25   2
#define B52   3
#define B100  4
#define B123  5

#define R3x   7 

#define P1 0
#define P2 1

#include <stdint.h>

int get_legal_actions(GameState* state, int8_t* out_actions) {
	uint8_t act = 0;
	if (state->active_player == P1) {
		if (state->num_actions_this_street) //previous actions, we're now allowed to fold
			out_actions[act++] = FOLD;
		out_actions[act++] = PASS; //chk / call	
		
		//we can only raise after IP has performed
		if (state->num_actions_this_street     && 
		    state->num_actions_this_street % 2 &&
		    state->raises_this_street < 3) {
			out_actions[act++] = B3x;
			//will add more...
		}
		else {
			out_actions[act++] = B10;
			out_actions[act++] = B25;
			out_actions[act++] = B52;
			out_actions[act++] = B100;
			out_actions[act++] = B123;
		}
	}
	else { //P2
		if (state->raises_this_street)//vs bet, not check
			out_actions[act++] = FOLD;
		out_actions[act++] = PASS;
		if (state->raises_this_street && state->raises_this_street < 3) //keep raise in order
			out_actions[act++] R3x;
		else {
			out_actions[act++] = B10;
			out_actions[act++] = B25;
			out_actions[act++] = B52;
			out_actions[act++] = B100;
			out_actions[act++] = B123;

		}
		
	}
}

void calculate_strategy(float* regret_sum, float* strategy, int num_actions, int num_buckets) {
	/*
	 * [[b1,b2,b3],[b1,b2,b3][..]...] flattened, regret_sum[action][bucket]
	 */
	for (int a = 0; a < num_actions; a++) {
		for (int b = 0 ; b < num_buckets; b++) {
			int id = (a * num_buckets) + b;
			float r = regret[id];
			strategy[id] = r < 0.0f ? 0 : r;
		}
	}

	for (int b = 0; b < num_buckets; b++) {
		float sum = 0.0f;

		for (int a = 0; a < num_actions; a++)
			sum += strategy[(a * num_buckets) + b];

		for (int a = 0; a < num_actions; a++) {
			int id = (a * num_buckets) + b;
			strategy[id] = sum > 0.0f ? 
				strategy[id] / sum :
				1.0f / (float)num_actions;
		}
	}
}

void action_node(PublicNode* node, GameState *state, ) {
	int8_t legal_actions[8]; //8b align 
	int action_count = get_legal_actions(state, legal_actions);

	float* strategy = (float*)malloc(num_actions * num_buckets * sizeof(float)); 
	float* act_util = (float*)malloc(num_actions * num_buckets * sizeof(float));

	
	//calc_strat
}
void dcfr(PublicNode* node, GameState* state) {
	if (node->type == NODE_ACTION) {
		action_node(node, state);
	}
}

