#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

void train(int iterations) {
	int i, j;
	int hero_action, villain_action;

	//current strat of playing rock / paper / scissors
	float* hero_strat    = malloc(sizeof(float) * 3);
	float* villain_strat = malloc(sizeof(float) * 3);

	//regret and sums
	float hero_regret[3] = {0,0,0}, hero_strat_sum[3] = {0,0,0};
	float villain_regret[3] = {0,0,0}, villain_strat_sum[3] = {0,0,0};

	float hero_util[3];
	float villain_util[3];

	float final_strategy[3];

	float temp_print_strat[3]; 

	printf("Iteration | Rock   | Paper  | Scissors\n");
	printf("--------------------------------------\n");

	for ( i = 0; i < iterations; i++) {
		//get strategies from past regrets
		get_strategy(hero_regret, hero_strat);
		get_strategy(villain_regret, villain_strat);
	

		//play the hand, random action
		hero_action    = get_action(hero_strat);
		villain_action = get_action(villain_strat);

		//what would have hero won against villains move?
		//we're just cycling out the move using modulo math
		hero_util[villain_action] = 0;            // tie
		hero_util[(villain_action + 1) % 3] = 1;  // win
		hero_util[(villain_action + 2) % 3] = -1; // lose

		//what would have villain won against hero moves?
		villain_util[hero_action] = 0;
		villain_util[(hero_action + 1) % 3] = 1;
		villain_util[(hero_action + 2) % 3] = -1;

		//update hero regret
		for (j = 0; j < 3; j++) {
			hero_regret[j]    += hero_util[j] - hero_util[hero_action];
			hero_strat_sum[j] += hero_strat[j];

			//update villain
			villain_regret[j]    += villain_util[j] - villain_util[villain_action];
			villain_strat_sum[j] += villain_strat[j];
		}
		if (i % 100 == 0) {
			// We use our helper function to Normalize the current sum 
			// into a readable percentage, just for printing.
			get_strategy(hero_strat_sum, temp_print_strat);

			printf("%9d | %.4f | %.4f | %.4f\n", 
                		i, 
                		temp_print_strat[0], 
                		temp_print_strat[1], 
                		temp_print_strat[2]
            		);
		}
	}
	get_strategy(hero_strat_sum, final_strategy);
    
	printf("Nash Equilibrium for Hero (%d iterations)\n", iterations);
	printf("Rock:     %.4f\n", final_strategy[0]);
	printf("Paper:    %.4f\n", final_strategy[1]);
	printf("Scissors: %.4f\n", final_strategy[2]);

	free(hero_strat);
	free(villain_strat);
}

/*
//hero    - rock  - 0
//villain - paper - 1
//          sciss - 2
hero_util[1] = 0;            // tie
hero_util[(1 + 1) % 3] = 1;  // win
hero_util[(1 + 2) % 3] = -1; // lose

//what would have villain won against hero moves?
villain_util[0] = 0;
villain_util[(0 + 1) % 3] = 1;
villain_util[(0 + 2) % 3] = -1;

//update hero regret
for (j = 0; j < 3; j++) {
	hero_regret[j]    += hero_util[j] - hero_util[0]; // -1
	hero_strat_sum[j] += hero_strat[j];
	// after loop - { 0, 1, 2} // scissors is biggest regret

	//update villain
	villain_regret[j]    += villain_util[j] - villain_util[1]; // 1
	villain_strat_sum[j] += villain_strat[j];
	// after loop -{ -1, 0, -2} //no positive regrets, perfect move, he won this hand
}

next loop - get_strategy
hero    - {0.00, 0.33, 0.67}
villain - {0.33, 0.33, 0.33} -> if normalizing sum <= 0 set equal

get_action - random 0.37
hero       - scissors
get_action - random 0.55
villain    - paper

 */
/*
This is technically exploitative, we're moving to a self-play solution

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
*/

int main() {
	printf("Solving Rock Paper Scissors...\n");

	srand(time(0));
	strategy_sum = malloc(sizeof(float) * 3);
	regret_sum   = malloc(sizeof(float) * 3);

	for (int i = 0; i < 3; i++) {
		strategy_sum[i] = 0.0;
		regret_sum[i]   = 0.0;
	}

	train(10);
	train(100);
	train(1000);
	train(10000);
	train(100000);
	train(1000000);
	return 0;
}

