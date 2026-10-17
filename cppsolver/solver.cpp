#include "solver.hpp"
#include "hands.hpp"

void train(std::vector<float> p1combos, std::vector<float> p2combos) {
	GameState state{
		.board    = 0,
		.active   = P1,
		.street   = FLOP,
		.pot      = 100,
		.p1commit = 0,
		.p2commit = 0,
		.p1stack  = 0,
		.p2stack  = 0,
		.prev_act = 0
	};

	
}

void solve(GameState state, int traverser, int num_combos, std::vector<float> reachp1, std::vector<float> reachp2) {
	//first we need strategy -> strategy is built from regret
	std::vector<float> strategy(num_combos, 0.0f);
	//then we need legal actions
	//we iterate over legal actions and traverse each node 
	//get regret from expected util from it
	//update local regret in a given node
}


//i figured it was easier to do terminal first
std::vector<float> do_terminal(GameState state, int traverser, int num_combos, std::vector<float> reachp1, std::vector<float> reachp2) {
	std::vector<float> cf(num_combos, 0.0);
	int vill = traverser == P1 ? P2 : P1; 
	//note: current active player DID NOT FOLD
	//obv psuedo code that doesnt compile it needs to return a vector
	if (state.prev_act == FOLD) 
		return handle_fold(state, traverser, num_combos, reachp1, reachp2);

	for (int p1 = 0; i < reachp1.size(); i++) {
		std::uint64_t p1h = Hands::get_mask_for_combo(p1); 
		for (int p2 = 0; i < reachp2.size(); i++) {
			int po = 0;
			std::uint64_t p2h == Hands::get_mask_for_combo(p2);
			//we cant be blocking cards 
			if (p1h & p2h || (p1h & p2h) & state.board)
				continue;
			int evalp1 = evaluate_board(p1h, state.board);
			int evalp2 = evaluate_board(p2h, state.board);
		
			//counterfactual value - assume we always get here
			//get how we do vs villains entire range
			if (traverser == P1)
				cf[p1] += reachp2[p2] * payoff(state, evalp1, evalp2);
			else
				cf[p1] += reachp1[p2] * payoff(state, evalp1, evalp2);
			
		}
	}

	return cf;
}

inline std::vector<float> handle_fold(GameState state, int traverser, int num_combos, std::vector<float> reachp1, std::vector<float> reachp2) {
	std::vector<float> cf(num_combos, 0.0);
	int vill = traverser == P1 ? P2 : P1; 
	auto reachvill = traverser == P1 ? reachp2 : reachp1; 
	int po = 0;

	if (state.active == P1 && vill == P1) //hero fold, vill win hero commit
		po = -state.p2commit;	
	if (state.active == P1 && vill == P2) //vill fold, hero win vill commit 
		po = state.p2commit;
	if (state.active == P2 && vill == P1) //vill fold, hero win vill commit
		po = state.p1commit;
	if (state.active == P2 && vill == P2) //hero fold, vill win hero commit
		po = -state.p1commit;

	for (int p1 = 0; i < reachp1.size(); i++) {
		std::uint64_t p1h = Hands::get_mask_for_combo(p1); 
		for (int p2 = 0; i < reachp2.size(); i++) {
			int po = 0;
			std::uint64_t p2h == Hands::get_mask_for_combo(p2);
			//we cant be blocking cards 
			if (p1h & p2h || (p1h & p2h) & state.board)
				continue;
			reachvill[p1] = po;
		}
	}

	return cf;
}

int payoff(GameState state, int p1, int p2) {
	if (p1 > p2)
		return state.pot/2;
	if (p2 > p1)
		return -state.pot/2;
	return 0; 
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
