#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdint.h>

#define PASS 0		//check or fold
#define BET  1		//bet or call
#define NUM_ACTIONS 2

#define P1 0
#define P2 1

#define ROOT	  0  //game root, start of game
#define P1_CHECK  1
#define P1_BET	2
#define P2_BET	3 
#define NUM_NODES 12

static uint32_t rng_state = 2463534242;

float mccfr(int, int, int, int);

typedef struct {
	float regretSum[2];
	float strategySum[2];
} Node;

Node node_map[NUM_NODES];

uint32_t xorshift32() {
	uint32_t x = rng_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	rng_state = x;
	return x;
}

float randf() {
	return (float)xorshift32() / (float)UINT32_MAX;
}

int randi(int max) {
	return xorshift32() % max;
}

void print_node_name(int index) {
	int card = index / 4;
	int hist = index % 4;
	
	char* card_names[] = {"Jack", "Queen", "King"};
	char* hist_names[] = {"Start", "Check", "Bet", "Check-Bet"};
	
	printf("%s facing %s", card_names[card], hist_names[hist]);
}


void get_strategy(float* in_regrets, float* out_strategy) {
	int i;
	float normalizing_sum;

	normalizing_sum = 0;

	for (i = 0; i < 2; i++) {
		out_strategy[i] = in_regrets[i] <= 0 ? 0 : in_regrets[i];
		normalizing_sum += out_strategy[i];
	}

	if (normalizing_sum > 0) 
		for ( i = 0; i < 2; i++)
			out_strategy[i] = out_strategy[i] / normalizing_sum;
	else
		for ( i = 0 ; i < 2; i++)
			out_strategy[i] = 1.00 / 2.00;
}

//randomly get pass (check/fold) or bet (bet/call)
int get_action(float* strategy) {
	double random, cumulative;
	int i;

	//0.0-1.0
	random = (double)randf() / RAND_MAX;
	cumulative = 0.0;

	for ( i = 0; i < 2; i++) {
		cumulative += strategy[i];

		if (random < cumulative)
			return i;
	}

	return BET; //or call! :)
}

int get_node_index(int card, int history) {
	//find what node we're in
	//Card 0 -> J, 1 -> Q, 2 -> K
	// * 4 for each round (root, p1check, p2check, p2bet)
	return (card * 4) + history;
}

// + 1 if P1 wins, -1 if P2 wins
float get_showdown_utility(int hero_card, int villain_card) {
	return (hero_card > villain_card) ? 1.0 : -1.0;
}

float get_payout(int hero_seat, int winner_seat) {
	return (hero_seat == winner_seat) ? 1.0 : -1.0;
}

float get_counterfactual_value(int action, int history, int hero_card, int villain_card, int hero_seat) {
	if (history == ROOT) {
		if (action == PASS) // hero, p1, check
			return mccfr(P1_CHECK, hero_card, villain_card, hero_seat);
		else		   // hero, p1, bet
			return mccfr(P1_BET,   hero_card, villain_card, hero_seat);
	}
	else if (history == P1_CHECK) {
		if (action == PASS) // P2 check, check/check
			return get_showdown_utility(hero_card, villain_card);
		else		   // P2 BET, check -> bet
			return mccfr(P2_BET, hero_card, villain_card, hero_seat);
	}
	else if (history == P1_BET) {
		if (action == PASS) //P2 folds
			return get_payout(hero_seat, P1);
		else		   //P2 calls, go to showdown to win $2
			return get_showdown_utility(hero_card, villain_card) * 2.0; //win whole pot
	}
	else if (history == P2_BET) {
		if (action == PASS) //Check, Bet, Fold, P2 wins $1
			return get_payout(hero_seat, P2);
		else		   //P1 calls, go to downdown for $2
			return get_showdown_utility(hero_card, villain_card) * 2.0;
	}

	return -1337; //should never get here but returning large value if we do
}
// Hero Seat = 0 -> Hero is P1
// Hero Seat = 1 -> Hero is P2
float mccfr(int history, int hero_card, int villain_card, int hero_seat) {
	int active_seat;
	int is_hero_turn;
	int node_index;
	int current_card;
	
	float strategy[2];
	float action_values[2];
	float ev = 0;

	//determine whos turn it is:
	if (history == ROOT || history == P2_BET)
		active_seat = 0; // seat 1 acts
	else
		active_seat = 1; // seat 2 acts
	
	is_hero_turn = (active_seat == hero_seat);
	current_card = is_hero_turn ? hero_card : villain_card;

	node_index = get_node_index(current_card, history);
	get_strategy(node_map[node_index].regretSum, strategy);

	//obv better way to prevent nesting but its easier to read.
	if (is_hero_turn) {
		int a;
		for ( a	= 0; a < 2; a++) {
			action_values[a] = get_counterfactual_value(a, history, hero_card, villain_card, hero_seat);
		}
	
		ev = (action_values[0] * strategy[0]) + (action_values[1] * strategy[1]);

		for ( a = 0; a < 2; a++) {
			float regret = action_values[a] - ev;
			
			node_map[node_index].regretSum[a]   += regret;
			node_map[node_index].strategySum[a] += strategy[a];
		}

		return ev;
	}
	else {
		//the special monte carlo sauce
		int action = get_action(strategy);
		return get_counterfactual_value(action, history, hero_card, villain_card, hero_seat);
	}
	
}

void train(int iterations) {
	int p1_card, p2_card;
	int hero_seat;

	for (int i = 0; i < iterations; i++) {
		p1_card = randi(3);
		do {
			p2_card = randi(3);
		}
		while (p1_card == p2_card);

		hero_seat = i % 2;

		mccfr(ROOT, p1_card, p2_card, hero_seat);
	}
}

int main() {
	int i, j;

	for ( i = 0; i < NUM_NODES; i++) {
		for ( j = 0; j < NUM_ACTIONS; j++) {
			node_map[i].regretSum[j]   = 0.0;
			node_map[i].strategySum[j] = 0.0;
		}
	}

	int iterations = 10000000;
	printf("Training for %d iterations...\n", iterations);
	train(iterations);
	printf("Training complete.\n\n");

	printf("=== FINAL STRATEGY ===\n");
	printf("Format: [Check/Fold %%] [Bet/Call %%]\n");

	for (int i = 0; i < 12; i++) {
		float total_strategy = node_map[i].strategySum[0] + node_map[i].strategySum[1];
		
		if (total_strategy > 0) {
			float prob_pass = node_map[i].strategySum[0] / total_strategy;
			float prob_bet  = node_map[i].strategySum[1] / total_strategy;
			
			print_node_name(i); 
			printf(": [%.2f] [%.2f]\n", prob_pass, prob_bet);
		}
	}
	return 0;
}
