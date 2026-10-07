#include "solver.hpp"
#include "hands.hpp"

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

void solve(GameState state, int active, int num_combos, std::vector<float> reachp1, std::vector<float> reachp2) {
	//first we need strategy -> strategy is built from regret
	//then we need legal actions
	//we iterate over legal actions and traverse each node 
	//get regret from expected util from it
	//update local regret in a given node
}

//i figured it was easier to do terminal first
std::vector<float> do_terminal(GameState state, int traverser, std::vector<float> reachp1, std::vector<float> reachp2) {
	std::vector<float> payoff(1326, 0.0);
	for (int p1 = 0; i < reachp1.size(); i++) {
		std::uint64_t p1h = Hands::get_mask_for_combo(p1); 
		for (int p2 = 0; i < reachp2.size(); i++) {
			std::uint64_t p2h == Hands::get_mask_for_combo(p2);
			//we cant be blocking cards
			if (p1h & p2h)
				continue;
			int w = get_winner(p1h, p2h);
			
		}
	}
}

int get_winner(std::uint64_t p1h, std::uint64_t p2h) {

}

//we should store regret as [actions][combos]
//each action[] has an infoset for a given combo
//note that combos is a less generalized form of "infoset"
std::vector<float> get_strategy(int legal_actions, int num_combos, const std::vector<float>& regret) {
	std::vector<float> strategy(legal_actions * num_combos, 0.0);
	std::vector<float> rsum(num_combos, 0.0); //positive regret sum per combo over A (all actions a)

	for (int i = 0; i < legal_actions; i++) {
		for (int j = 0; j < num_combos; j++) {
			int regret_i = (i*num_combos) + j;
			float r = std::max(0.0, regret[regret_i]);
			strategy[regret_i] = r;
			rsum[j] += r;
		}
	}

	//negative regret for all a in A over infoset I means we have evenly distributed strategy across all actions
	const float normalized_strat = 1.0 / legal_actions; 

	for (int i = 0; i < legal_actions; i++) {
		for (int j = 0; j < num_combos; j++) {
			int regret_i = (num_combos * i) + j;
			float rsum_val = rsum[j]; //for infoset
			if (rsum_val <= 0)
				strategy[regret_i] = normalized_strat; 
			else
				strategy[regret_i] = strategy[regret_i] / rsum_val;
		}
	}

	return strategy;
}
