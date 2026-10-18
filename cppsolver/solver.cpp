#include "solver.hpp"
#include "hands.hpp"
#include "evaluator.hpp"

void Solver::train(std::vector<float> p1combos, std::vector<float> p2combos) {
	GameState state{
		.board    = 0,
		.active   = P1,
		.prev_act = -1,
		.street   = FLOP,
		.pot      = 40,
		.p1commit = 0,
		.p2commit = 0,
		.p1stack  = 80,
		.p2stack  = 80,
		.raises   = 0
	};
}

std::vector<float> Solver::solve(GameState state, Node& node, int traverser, int num_combos, std::vector<float> reachp1, std::vector<float> reachp2, std::vector<float> cf) {
	/*
	if (node.type == ACTION)
		return do_action();
	if (node.type == CHANCE) 
		return do_chance();
	if (node.type == TERMINAL)
		return do_terminal();
	*/
}

std::vector<float> Solver::do_action(GameState state, Node& node, int traverser, int num_combos, std::vector<float> reachp1, std::vector<float> reachp2, std::vector<float> cf) {
	std::vector<float> cf_new;
	//first we need strategy -> strategy is built from regret
	//array size [legal_actions][num_combos]
	auto strategy = get_strategy(legal, num_combos, node.regret);
	//then we need legal actions
	auto legal_actions = get_legal_actions(state);
	//we iterate over legal actions and traverse each node 
	for (auto action : legal_actions) {
		auto next_p1reach = reachp1;
		auto next_p2reach = reachp2;
		auto& act_reach = state.active == P1 ? next_reachp1 : next_reachp2;
	
		int act_index = action * num_combos;
		for (int c = 0; c < num_combos; c++)
			act_reach[c] *= strategy[act_index + c];
		
		auto next = next_state(state, action);

	}
		
	//get regret from expected util from it
	//update local regret in a given node

	return cf_new; 
}

//note that commit is a RUNNING TALLY
//TODO - fix, we need several improvements here...
//raises are 2.5x, bet is 50% for now
GameState Solver::next_state(GameState prev, int action) {
	auto next = prev;
	auto& active_commit   = prev.active == P1 ? prev.p1commit : prev.p2commit;
	auto& inactive_commit = prev.active == P1 ? prev.p2commit : prev.p1commit;
	auto& active_stack    = prev.active == P1 ? prev.p1stack  : prev.p2stack;
	auto& inactive_stack  = prev.active == P1 ? prev.p2stack  : prev.p1stack;

	next.active = prev.active == P1 ? P2 : P1;
	next.prev_act = action;
	auto& next_commit = next.active == P1 ? next.p1commit : next.p2commit; 
	auto& next_stack  = next.stack  == P1 ? next.p1stack  : next.p2stack; 

	if (action == RAISE) {
		bool is_bet_not_raise = prev.prev_act != RAISE && action == RAISE;
		int bet_amt = is_bet_not_raise ?
			pot * 0.5 :
			active_commit - inactive_commit * RAISE_AMT;
		next.raises++;
		next_stack -= bet_amt;
		next_commmit += bet_amt;
		next_pot += bet_amt
	}
	else if (action == PASS && prev.prev_act == RAISE) {
		int = active_commit - inactive_commit;
		next.raises = 0;
		next_stack -= bet_amt;
		next_commmit += bet_amt;
		next_pot += bet_amt

	}
	if (prev.prev_act == PASS && action == PASS)
		next.street++;

	return state;
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
