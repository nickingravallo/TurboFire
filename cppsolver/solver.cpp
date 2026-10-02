#include "solver.hpp"

void train(std::vector<float> p1combos, std::vector<float> p2combos) {
	GameState state{
		.street   = FLOP,
		.pot      = 100,
		.p1commit = 0,
		.p2commit = 0,
		.p1stack  = 0,
		.p2stack  = 0
	};

	
}

void solve(GameState state, int active, std::vector<float> reachp1, std::vector<float> reachp2) {
	//first we need strategy -> strategy is built from regret
	//then we need legal actions
	//we iterate over legal actions and traverse each node 
	//get regret from expected util from it
	//update local regret in a given node
}

//we should store regret as [actions][combos]
std::vector<double> get_strategy(int legal_actions, int num_combos, const std::vector<double>& regret) {
	vector<double> a(legal_actions * num_combos);
	vector<double> rsum(num_combos, 0);

	for (int i = 0; i < legal_actions; i++) {
		for (int j = 0; j < num_combos; j++) {
			int c = (i*num_combos) + j;
			double r = regret[c];
			a[c] += r;
			rsum[j] += r;
		}
	}

	//todo fix
	for (int i = 0; i < legal_actions; i++) {
		for (int j = 0; j < num_combos; j++) {
			int r = (num_actions * i) + j;
			if (rsum <= 0)
				a[r] = 1 / rsum;
			else
				a[r] = a[r] / strategy_sum;
		}
	}

	return a;
}
