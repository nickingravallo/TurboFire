#include "solver.hpp"
#include "hands.hpp"
#include "evaluator.hpp"

void Solver::train(std::vector<float> p1combos, std::vector<float> p2combos) {
	GameState state{
		.board    = 0,
		.active   = P1,
		.prev_act = -1,
		.street   = FLOP,
		.pot      = 100,
		.p1commit = 0,
		.p2commit = 0,
		.p1stack  = 0,
		.p2stack  = 0,
		.raises   = 0
	};
}

void Solver::solve(GameState state, Node node, int traverser, int num_combos, std::vector<float> reachp1, std::vector<float> reachp2) {
	//first we need strategy -> strategy is built from regret
	auto strategy = get_strategy(legal, num_combos, node.regret);
	//then we need legal actions
	auto legal_actions = get_legal_actions(state);
	//we iterate over legal actions and traverse each node 
		
	//get regret from expected util from it
	//update local regret in a given node
}


//i figured it was easier to do terminal first
std::vector<float> Solver::do_terminal(GameState state, int traverser, int num_combos, std::vector<float> reachp1, std::vector<float> reachp2) {
	std::vector<float> cf(num_combos, 0.0);
	int vill = traverser == P1 ? P2 : P1;
	//note: current active player DID NOT FOLD
	if (state.prev_act == FOLD) 
		return handle_fold(state, traverser, num_combos, reachp1, reachp2);

	for (int p1 = 0; p1 < reachp1.size(); p1++) {
		std::uint64_t p1h = Hands::get_mask_for_combo(p1); 
		for (int p2 = 0; p2 < reachp2.size(); p2++) {
			std::uint64_t p2h = Hands::get_mask_for_combo(p2);
			//we cant be blocking cards 
			if ((p1h & p2h) || ((p1h | p2h) & state.board))
				continue;
			int evalp1 = evaluate_board(p1h, state.board);
			int evalp2 = evaluate_board(p2h, state.board);

			//ev would be ev[p1] * hero_reach[x] * vill_reach[y] * payoff but we assume hero always gets here
			//P1 hero
			if (vill == P2)
				cf[p1] += reachp2[p2] * payoff(state, evalp1, evalp2, P1);
			else
				cf[p2] += reachp1[p1] * payoff(state, evalp1, evalp2, P2);
		}
	}

	return cf;
}

inline std::vector<float> Solver::handle_fold(GameState state, int traverser, int num_combos, std::vector<float> reachp1, std::vector<float> reachp2) {
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

	for (int p1 = 0; p1 < reachp1.size(); p1++) {
		std::uint64_t p1h = Hands::get_mask_for_combo(p1); 
		for (int p2 = 0; p2 < reachp2.size(); p2++) {
			std::uint64_t p2h = Hands::get_mask_for_combo(p2);
			//we cant be blocking cards 
			if ((p1h & p2h) || ((p1h | p2h) & state.board))
				continue;
			cf[p1] += po * reachvill[p2];
		}
	}

	return cf;
}

int Solver::payoff(GameState state, int p1, int p2, int trav) {
	int p = 0;
	//this assumes p1 perspective
	if (p1 > p2)
		p = state.pot/2;
	else if (p2 > p1)
		p = -state.pot/2;

	return trav == P1 ? p : -p;
}

/*
TODO - WE NEED TO DO CHIP CHECKING! 
 */
//check / call same value as they are the passive action
std::vector<int> Solver::get_legal_actions(const GameState& state) {
	std::vector<int> actions;
	//passive action always an option
	actions.push_back(PASS);
	//first to act
	if (state.prev_act == NONE || state.prev_act == PASS) {
		if (state.raises < MAX_RAISES) 
			actions.push_back(RAISE);
		return actions;
	}

	if (state.prev_act == RAISE) {
		actions.push_back(FOLD);
		if (state.raises < MAX_RAISES) 
			actions.push_back(RAISE);		
		return actions;
	}

	if (state.prev_act == FOLD) {
		std::cout << "state.prev_act was fold in get_legal_actions. BAD!\n";
		return actions;
	}

	std::cout << "state.prev_act out of band, really bad!\n";
	return actions;
}

//we should store regret as [actions][combos]
//each action[] has an infoset for a given combo
//note that combos is a less generalized form of "infoset"
std::vector<float> Solver::get_strategy(int legal_actions, int num_combos, const std::vector<float>& regret) {
	std::vector<float> strategy(legal_actions * num_combos, 0.0);
	std::vector<float> rsum(num_combos, 0.0); //positive regret sum per combo over A (all actions a)

	for (int i = 0; i < legal_actions; i++) {
		for (int j = 0; j < num_combos; j++) {
			int regret_i = (i*num_combos) + j;
			float r = std::max(0.0f, regret[regret_i]);
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
