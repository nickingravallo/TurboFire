/*
 * What we need:
 *  - strategySum[3];
 *  - regretSum[3];
 *
 *  get random action
 *  get the strategy by normalizing
 *  calculating regret
 */

#include <stdio.h>
#include <stdlib.h>

//add up to 1
float regretSum[3];   
float strategySum[3];

void get_strategy(float*, float*);
int get_action(float*);
void train(int);

void get_strategy(float* regret, float* strategy_out) {
	float normalized_strategy;
	int i;

	for ( i = 0 ; i < 3 ; i ++ )
		strategy_out[i] = regret[i] <= 0 ? 0 : regret[i];

	normalized_strategy = strategy_out[0] + strategy_out[1] + strategy_out[2];

	if ( normalized_strategy > 0 ) 
		for ( i = 0; i < 3; i++)
			strategy_out[i] = strategy_out[i] / normalized_strategy;
	else
		for ( i = 0 ; i < 3; i++ )
			strategy_out[i] = 1.0 / 3.0; //equal strategy
}

int get_action(float* strategy) {
	float r;
	int i;

	float strategy_sum;
	
	strategy_sum = 0;
	r = (double)rand() / (double)RAND_MAX;

	for ( i = 0 ; i < 3 ; i++) {
		strategy_sum += strategy[i];

		if ( r < strategy_sum )
			return i;
	}

	return 2;
}

void train(int iterations) {
	int i, j;
	
	int player1_action, player2_action;
	
	float player1_strategy[3] = {0, 0, 0};
	float player2_strategy[3] = {0, 0, 0};

	float player1_regret[3] = {0,0,0};
	float player2_regret[3] = {0,0,0};

	float player1_strategy_sum[3] = {0,0,0};
	float player2_strategy_sum[3] = {0,0,0};

	float player1_counterfactual_payoff[3] = {0,0,0};
	float player2_counterfactual_payoff[3] = {0,0,0};

	float player1_final_strategy[3], player2_final_strategy[3];

	for ( i = 0 ; i < iterations; i++ ) {
		get_strategy(player1_regret, player1_strategy);	
		get_strategy(player2_regret, player2_strategy);

		player1_action = get_action(player1_strategy);
		player2_action = get_action(player2_strategy);

		//what would have happened if player1 had taken player 2's strategy?
		player1_counterfactual_payoff[player2_action] = 0;            // tie
		player1_counterfactual_payoff[(player2_action + 1) % 3] = 1;  // win
		player1_counterfactual_payoff[(player2_action + 2) % 3] = -1; // lose

		//mirrored for player 2
		player2_counterfactual_payoff[player1_action] = 0;            // tie
		player2_counterfactual_payoff[(player1_action + 1) % 3] = 1;  // win
		player2_counterfactual_payoff[(player1_action + 2) % 3] = -1; // lose
	
		for ( j = 0; j < 3; j++) {
			player1_regret[j] += player1_counterfactual_payoff[j] - player1_counterfactual_payoff[player1_action];
			player1_strategy_sum[j] += player1_strategy[j];

			player2_regret[j] += player2_counterfactual_payoff[j] - player2_counterfactual_payoff[player2_action];
			player2_strategy_sum[j] += player2_strategy[j];
		}

		if (i > 0 && i % 10000 == 0) {
			printf("Strategy after %d iterations\n", i);
			printf("Player1 Strategy:\n");
			printf("Rock:     %.2f\n", player1_strategy_sum[0]);
			printf("Paper:    %.2f\n", player1_strategy_sum[1]);
			printf("Scissors  %.2f\n", player1_strategy_sum[2]);
	
			printf("Player2 Strategy:\n");
			printf("Rock:     %.2f\n", player2_strategy_sum[0]);
			printf("Paper:    %.2f\n", player2_strategy_sum[1]);
			printf("Scissors  %.2f\n", player2_strategy_sum[2]);

		}
	}

	//noramlize
	get_strategy(player1_strategy_sum, player1_final_strategy);
	get_strategy(player2_strategy_sum, player2_final_strategy);

	printf("Trained for %d iterations: \n", iterations);
	printf("Player1 Strategy Sum:\n");
	printf("Rock:     %.2f\n", player1_final_strategy[0]);
	printf("Paper:    %.2f\n", player1_final_strategy[1]);
	printf("Scissors  %.2f\n", player1_final_strategy[2]);
	
	printf("Player2 Strategy Sum:\n");
	printf("Rock:     %.2f\n", player2_final_strategy[0]);
	printf("Paper:    %.2f\n", player2_final_strategy[1]);
	printf("Scissors  %.2f\n", player2_final_strategy[2]);
}

void main() {
	train(100000);
}
