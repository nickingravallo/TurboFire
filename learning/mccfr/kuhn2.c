#include <stdio.h>
#include <stdlib.h>


typedef struct Node {
	float regret_sum[2];
	float strategy_sum[2];
} Node;

Node node_map[12];

#define GAME_ROOT 0
#define P1_PASS   1
#define P1_BET    2
#define P2_BET    3

#define PASS      0
#define BET       1

#define P1        0
#define P2        1

float mccfr(int, int, int, int);

int get_action(float* strategy) {
	float r;
	
	r = (float)rand() / (float)RAND_MAX;

	if (r < strategy[0])
		return PASS;
	return BET;
}


void print_node_name(int index) {
	int card = index / 4;
	int hist = index % 4;
	
	char* card_names[] = {"Jack", "Queen", "King"};
	char* hist_names[] = {"Start", "Check", "Bet", "Check-Bet"};
	
	printf("%s facing %s", card_names[card], hist_names[hist]);
}



void get_strategy(float* regret, float* out_strategy) {
	int i;

	float normalize_sum;

	//normalize
	for ( i = 0 ; i < 2; i++ )
		out_strategy[i] = regret[i] <= 0 ? 0 : regret[i];

	normalize_sum = out_strategy[PASS] + out_strategy[BET];

	if ( normalize_sum > 0 ) //there's some regret
		for ( i = 0 ; i < 2; i++)
			out_strategy[i] = out_strategy[i] / normalize_sum;
	else //we want equal strat if we have no fomo!
		for ( i = 0 ; i < 2 ; i++)
			out_strategy[i] = 1.0 / 2.0;
}

int get_node_key(int card, int history) {
	return (card * 4) + history; //4 histroies, 3 cards
}

float showdown(int p1_card, int p2_card, int traverser) {
	int winner;

	winner = (p1_card > p2_card) ? P1 : P2;
	return (traverser == winner) ? 1.0 : -1.0;
}

float get_payout(int traverser, int winner) {
	return (traverser == winner) ? 1.0 : -1.0;
}

float get_counterfactual_expected_value(int action, int history, int p1_card, int p2_card, int traverser) {
	if (history == GAME_ROOT) {
		if (action == PASS)
			return mccfr(P1_PASS, p1_card, p2_card, traverser);
		else
			return mccfr(P1_BET, p1_card, p2_card, traverser);
	}
	else if (history == P1_PASS) {
		if (action == PASS)
			return showdown(p1_card, p2_card, traverser);
		else
			return mccfr(P2_BET, p1_card, p2_card, traverser);
	}
	else if (history == P1_BET) {
		if (action == PASS) //technically a fold here
			return get_payout(traverser, P1);
		else
			return showdown(p1_card, p2_card, traverser) * 2.0;
	}
	else if (history == P2_BET) {
		if (action == PASS) //P1 fold
			return get_payout(traverser, P2);
		else
			return showdown(p1_card, p2_card, traverser) * 2.0;
	}

	printf("ERROR: get_counterfactual_value found invalid history!\n");
	return -1337;
}

float mccfr(int history, int p1_card, int p2_card, int traverser) {
	int action_player;
	int action_player_card;
	int is_traverser_turn;
	int node_key;
	float action_evs[2];

	Node* current_node;

	float strategy[2] = {0, 0};

	if (history == GAME_ROOT || history == P2_BET) {
		action_player      = P1;
		action_player_card = p1_card;
	}
	else {
		action_player      = P2;
		action_player_card = p2_card;
	}

	is_traverser_turn  = (traverser == action_player);
	
	node_key = get_node_key(action_player_card, history);
	current_node = &node_map[node_key];

	get_strategy(current_node->regret_sum, strategy);
	if (is_traverser_turn) {
		int a;
		float node_ev;

		action_evs[PASS] = get_counterfactual_expected_value(PASS, history, p1_card, p2_card, traverser);
		action_evs[BET] = get_counterfactual_expected_value(BET, history, p1_card, p2_card, traverser);
		node_ev = (action_evs[PASS] * strategy[PASS]) + (action_evs[BET] * strategy[BET]);
		for ( a = 0 ; a < 2 ; a++ ) {
			float regret;

			regret = action_evs[a] - node_ev;

			current_node->regret_sum[a]   += regret;
			current_node->strategy_sum[a] += strategy[a];
		}

		return node_ev;
	}
	else {
		int action;

		action = get_action(strategy);
		return get_counterfactual_expected_value(action, history, p1_card, p2_card, traverser);
	}

	return -1337; //we shouldnt get here lol
}

int main() {
	int i;
	int traverser;

	int p1_card, p2_card;

	for (i = 0 ; i < 10000000; i++ ) {
		p1_card = rand() % 3;
		do {
			p2_card = rand() % 3;
		}
		while (p1_card == p2_card);

		traverser = i % 2;

		mccfr(GAME_ROOT, p1_card, p2_card, traverser);
	}

	printf("Format: [Check/Fold %%] [Bet/Call %%]\n");
	for (i = 0; i < 12; i++) {
		float total_strategy = node_map[i].strategy_sum[0] + node_map[i].strategy_sum[1];
		
		if (total_strategy > 0) {
			float prob_pass = node_map[i].strategy_sum[0] / total_strategy;
			float prob_bet  = node_map[i].strategy_sum[1] / total_strategy;
			
			print_node_name(i); 
			printf(": [%.2f] [%.2f]\n", prob_pass, prob_bet);
		}
	}
	return 0;
}
