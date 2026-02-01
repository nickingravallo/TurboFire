#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int actions[3] = {0, 1, 2};
float* regret_sum;
float* strategy_sum;

void get_strategy(float* input_values, float* strategy) {
	int i;
	float normalizing_sum;

	//ensure regrets are non neg
	for (i = 0; i < 3; i++)
		strategy[i] = input_values[i] <= 0 ? 0 : input_values[i];
	
	normalizing_sum = strategy[0] + strategy[1] + strategy[2];

	//normalize to prob distrib
	if (normalizing_sum > 0) 
		for (i = 0; i < 3; i++)
			strategy[i] = strategy[i] / normalizing_sum;
	else
		for (i = 0; i < 3; i++)
			strategy[i] = 1.0 / 3.0;
}

//weighted random selector for action of rock paper scissors
int get_action(float* strategy) {
	double random, cumulative;
	int i;

	//0.0-1.0
	random = (double)rand() / RAND_MAX;
	cumulative = 0.0;

	for ( i = 0; i < 3; i++ ) {
		cumulative += strategy[i];

		if (random < cumulative)
			return i;
	}

	return 2;
}

//this is bad switch to diff strategy
void train(int iterations) {
	int i, j;
	int hero_action, villain_action;
	float* strategy;
	float* gto_strat; //populate perfect strat
	float* final_strategy;
	float action_utility[3] = {0.0, 0.0, 0.0};

	final_strategy = malloc(sizeof(float) * 3);
	gto_strat = malloc(sizeof(float) * 3);
	for ( i = 0; i < 3; i++ )
		gto_strat[i] = 1.0 / 3.0;

	strategy = malloc(sizeof(float) * 3);
	memset(strategy, 0, sizeof(float)*3);
	memset(final_strategy, 0, sizeof(float)*3);

	for (i = 0; i < iterations; i++) {
		get_strategy(regret_sum, strategy);

		hero_action    = get_action(strategy);
		villain_action = get_action(gto_strat);
		
		action_utility[villain_action] = 0;
		action_utility[(villain_action + 1) % 3] = 1; //move that beats opponent
		action_utility[(villain_action + 2) % 3] = -1; //move that loses to opponent
		
		//update regrets
		for ( j = 0; j < 3; j++) {
			regret_sum[j] += action_utility[j] - action_utility[hero_action];
			strategy_sum[j] += strategy[j];
		}

	}
	printf("Final strategy: \n");
	get_strategy(strategy_sum, final_strategy);
	printf("Rock:     %f\n", final_strategy[0]);
	printf("Paper:    %f\n", final_strategy[1]);
	printf("Scissors: %f\n", final_strategy[2]);
}

int main() {
	printf("Solving Rock Paper Scissors...\n");

	srand(time(0));
	strategy_sum = malloc(sizeof(float) * 3);
	regret_sum   = malloc(sizeof(float) * 3);

	for (int i = 0; i < 3; i++) {
		strategy_sum[i] = 0.0;
		regret_sum[i]   = 0.0;
	}

	train(10000);
	return 0;
}

