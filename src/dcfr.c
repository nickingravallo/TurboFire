
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

void calculate_strategy(float* regret_sum, float* strategy, int num_actions, int num_combos) {
	/*
	 * [[b1,b2,b3],[b1,b2,b3][..]...] flattened, regret_sum[action][bucket]
	 * We're getting positive regret_sum and prepping them for action weighting 
	 */
	for (int a = 0; a < num_actions; a++) {
		for (int b = 0 ; b < num_combos; b++) {
			int id = (a * num_combos) + b;
			float r = regret_sum[id];
			strategy[id] = r <= 0.0f ? 0 : r;
		}
	}

	//per combo, for each action, coming up with a strategy (0.0-1.0) based on normalizing the regrets for a specific combo
	for (int b = 0; b < num_combos; b++) {
		float sum = 0.0f;

		for (int a = 0; a < num_actions; a++)
			sum += strategy[(a * num_combos) + b];
		
		//normalize strategy
		for (int a = 0; a < num_actions; a++) {
			int id = (a * num_combos) + b;
			strategy[id] = sum > 0.0f ? 
				strategy[id] / sum :
				1.0f / (float)num_actions;
		}
	}
}

GameState apply_bet(GameState current_state, int action) {
	GameState = next_state = current_state;
	next_state.num_actions_this_street += 1;

	switch (action) {
		case FOLD:
			next_state.last_action_was_fold = 1;
			return next_state;
		case PASS:
			int bet = current_state.p2_commit - current_state.p1_commit;
			int (current_state.active_player == 1)
				bet = -bet; 

			if (bet > 0)
				next_state.pot += bet;
	}
}

void action_node(PublicNode* node, GameState *state, int num_combos, float* p1_reach, float* p2_reach, float* expected_util) {
	int8_t legal_actions[8]; //8b align 
	uint8_t active = node->active_player;
	int action_count = get_legal_actions(state, legal_actions);

	float* strategy = (float*)malloc(num_actions * num_combos * sizeof(float)); 
	float* act_util = (float*)malloc(num_actions * num_combos * sizeof(float));

	calculate_strategy(node->regret_sum, strategy, num_actions, num_combos);

	for (int action = 0; i < action_count; i++) {
		float* next_p1_reach = (float*) malloc(num_combos*sizeof(float));
		float* next_p2_reach = (float*) malloc(num_combos*sizeof(float));
		memcpy(next_p1_reach, p1_reach, sizeof(float) * num_combos);
		memcpy(next_p2_reach, p2_reach, sizeof(float) * num_combos);

		/*
		 * We're updating the reach probability for each player, for a given action, for each combo
		 * We have our initial weightings due to preflop ranges, and then certain hands will not have
		 * the same weighting due cards literally being better than others. 72o will not be weighted
		 * the same way as AA.
		 */
		for (int combo = 0; combo < num_combos; combo++) {
			int idx = (a * num_combos) + combo;
			if (active_player == 0)
				next_p1_reach[b] *= strategy[idx];
			else
				next_p2_reach[b] *= strategy[idx]
		}

		GameState next_state = apply_bet(state, legal_actions[action]);
	}
	
}
void dcfr(PublicNode* node, GameState* state) {
	if (node->type == NODE_ACTION) {
		action_node(node, state);
	}
}

