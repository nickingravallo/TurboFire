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
	int bet_size = 0;

	switch (action) {
		case FOLD:
			next_state.last_action_was_fold = 1;
			return next_state;
		case PASS:
			bet_size = current_state.p2_commit - current_state.p1_commit;
			int (current_state.active_player == 1)
				bet_size = -bet_size; 
		case B10:
			bet_size = current_state.pot * 0.1;
			break:
		case B25:
			bet_size = current_state.pot * 0.25;
			break;
		case B52:
			bet_size = current_state.pot * 0.52;
			break;
		case B100:
			bet_size = current_state.pot;
			break;
		case B123:
			bet_size = current_state.pot * 1.23;
			break;
		case R3x:
			int commit = current_state.p2_commit - current_state.p1_commit;
			int (current_state.active_player == 1)
				commit = -commit; 

			bet_size = commit * 3;
			next_state.raises_this_street += 1;
			break;
		default:
			printf("ERR: BAD ACTION IN APPLY_BET %d\n", action);
	}
	
	if (next_state.active_player == 0) {
		next_state.p1_commit += bet_size;
		next_state.p1_stack  -= bet_size;
	}
	else {
		next_state.p2_commit += bet_size;
		next_state.p2_stack  -= bet_size;
	}

	next_state.pot += bet_size;

	next_state.active_player = 1 - next_state.active_player;
	
	return next_state;
}

void action_node(PublicNode* node, GameState *state, int num_combos, float* p1_reach, float* p2_reach, float* expected_util) {
	int8_t legal_actions[8]; //8b align 
	uint8_t active = node->active_player;
	int action_count = get_legal_actions(state, legal_actions);

	float* strategy = (float*)malloc(action_count * num_combos * sizeof(float)); 
	float* action_expected_util = (float*)malloc(action_count * num_combos * sizeof(float));

	calculate_strategy(node->regret_sum, strategy, action_count, num_combos);

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
				next_p2_reach[b] *= strategy[idx];
		}

		GameState next_state = apply_bet(state, legal_actions[action]);
		
		//we calculate utility on a per-action basis, per node
		float* child_expected_util = &action_expected_util[action * num_combos];
		walk_tree(node->children[action], next_state, num_combos, next_p1_reach, next_p2_reach, child_expected_util);

		/*
		 * update utility, the child node is from perspective of next player so we must reverse it
		 * expected_util[combo] += strategy[action][combo] * child_expected_util[combo]	
		 * we are getting the expected_util by the EV of the next node for our selected combos for the node (on a per action basis)
		 * this is converted into the perspective of our node
		 * we multiply it by strategy since we're choosing this action with this combo 100% of the time, only a certain broken down percentage
		*/
		for (int combo = 0; combo < num_combos; combo++) {
			child_expected_util[combo] = -child_expected_util[combo];
			expected_util[combo] += strategy[(action * num_combos) + combo] * child_expected_util[combo];
		}

		free(next_p1_reach);
		free(next_p2_reach);
	}
	
	/*
	 * Update Regrets
	 * regret = action EV for combo - average EV for combo
	 * we update reaches since we're not hitting every combo 100% of the time
	 * We update regret based on the chance the opponent will take an combo of an action
	 * We update strategy based on the chance we'll perform a specific combo of an action
	 */
	for (int action = 0; action < action_count, action++) {
		for (int combo = 0; combo < num_combos; combo++) {
			int id = (action * num_combos) + combo;
			
			float regret = action_expected_util[id] - expected_util[combo];

			float opp_reach = (active == 0) ? p2_reach[combo] : p1_reach[combo];
			float my_reach  = (active == 0) ? p1_reach[combo] : p2_reach[combo];

			node->regret_sum[id] += regret * opp_reach;
			node->strategy_sum[id] += strategy[id] * my_reach;

		}
	}

	free(strategy);
	free(action_utils);
}	


void terminal_node(PublicNode* node, GameState *state, int num_combos, float* p1_reach, float* p2_reach, float* expected_util) {
	//we're finally setting the EV
	memset(out_util, 0, num_buckets * sizeof(float));	

	//It's really simple if we have a fold, we're just counting commits
	if (state.last_action_was_fold) {
		for (int combo = 0; combo < num_combos; combo++) {
			if (state.active_player == 0)
				expected_util[combo] = -(float)state.p1_commit;
			else
				expected_util[combo] =  -(float)state.p2_commit;
		}
		return;
	}

	int combo_scores[1326] = {0};
	for (int combo = 0; combo < num_combos; combo++) {
		//TODO
		//evalaute_board()
		//get hand mask
	}

	for (int p1c = 0; p1c < num_combos; p1c++) {
		if (p1_reach[p1c] == 0.0f)
			continue;

		float ev = 0.0f;

		for (int p2c = 0; p2c < num_combos; p2c++) {
			if (p2_reach[p2c] == 0.0f)
				continue;
			int p1_score = combo_scores[p1c];
			int p2_score = combo_scores[p2c];

			if (p1_score > p2_score)
				ev += p2_reach[p2c] * (float)state.p2_commit;
			else if (p2_score > p1_score)
				ev -= p2_reach[p2c] * (float)state.p1_commit;
		}

		if (state.active_player == 0)
			expected_util[p1c] = ev;
		else
			expected_util[p2c] = ev;
	}
}



void dcfr(PublicNode* node, GameState *state, int num_combos, float* p1_reach, float* p2_reach, float* expected_util) {
	if (node->type == NODE_ACTION) {
		action_node(PublicNode* node, GameState *state, int num_combos, float* p1_reach, float* p2_reach, float* expected_util);
	}
	else if (node->type == NODE_TERMINAL) {
		terminal_node(PublicNode* node, GameState *state, int num_combos, float* p1_reach, float* p2_reach, float* expected_util);
	}
}

