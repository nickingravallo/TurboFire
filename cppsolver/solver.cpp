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

std::vector<float> Solver::solve(GameState state, Node& node, int traverser, int num_combos, std::vector<float> reachp1, std::vector<float> reachp2) {
	if (node.type == ACTION)
		return do_action(state, node, traverser, num_combos, reachp1, reachp2);
	if (node.type == CHANCE) 
		std::cout << "chance not implemented yet\n";
	if (node.type == TERMINAL)
		return do_terminal(state, traverser, num_combos, reachp1, reachp2);
}

std::vector<float> Solver::do_action(GameState state, Node& node, int traverser, int num_combos, std::vector<float> reachp1, std::vector<float> reachp2) {
	//first we need strategy -> strategy is built from regret
	//array size [legal_actions][num_combos]
	//then we need legal actions
	auto legal_actions = get_legal_actions(state);
	auto strategy = get_strategy(legal_actions.size(), num_combos, node.regret_sum);
	std::vector<float> action_cf(num_combos * legal_actions.size(), 0.0);
	std::vector<float> node_cf(num_combos, 0.0);
	//we iterate over legal actions and traverse each node 
	for (int a = 0; a < legal_actions.size(); a++) {
		int action = legal_actions[a];
		auto next_p1reach = reachp1;
		auto next_p2reach = reachp2;
		auto& act_reach = state.active == P1 ? next_p1reach : next_p2reach;
	
		int act_index = a * num_combos;
		for (int c = 0; c < num_combos; c++)
			act_reach[c] *= strategy[act_index + c];
		
		auto next = next_state(state, action);
		//"counterfactual" bc excludes hero's prob which we will retroactively correct with * strategy for each infoset returned
		auto child_cf = solve(next, node.children[a], traverser, num_combos, next_p1reach, next_p2reach);

		//faster for branch pred
		if (traverser == state.active) {
			for (int i = 0; i < node_cf.size(); i++) {
				int aci = a * num_combos + i;
				node_cf[i]    += strategy[aci] * child_cf[i];
					action_cf[aci] = child_cf[i];
			}
		}
		else {
			for (int i = 0; i < node_cf.size(); i++) {
				node_cf[i] += child_cf[i];
			}
		}

	}
		
	//once again, dont update for non-trav
	if (traverser != state.active)
		return node_cf;
	const auto& own_reach = state.active == P1 ? reachp1 : reachp2;

	//get regret from expected util from it
	//update local regret in a given node
	for (int a = 0; a < legal_actions.size(); a++) {
		for (int c = 0; c < num_combos; c++) {
			int aci = a * num_combos + c;
			node.regret_sum[aci]   += action_cf[aci] - node_cf[c];
			node.strategy_sum[aci] += own_reach[c] * strategy[aci];
		}
	}

	return node_cf; 
}

//note that commit is a RUNNING TALLY
//TODO - fix, we need several improvements here...
//raises are 2.5x, bet is 50% for now
GameState Solver::next_state(GameState prev, int action) {
	auto next = prev;
	const int active_commit = prev.active == P1 ? prev.p1commit : prev.p2commit;
	const int inactive_commit = prev.active == P1 ? prev.p2commit : prev.p1commit;
	const int inactive_stack = prev.active == P1 ? prev.p2stack : prev.p1stack;
	auto& next_commit = prev.active == P1 ? next.p1commit : next.p2commit;
	auto& next_stack = prev.active == P1 ? next.p1stack : next.p2stack;

	next.active = prev.active == P1 ? P2 : P1;
	next.prev_act = action;
	const int to_call = std::max(0, inactive_commit - active_commit);

	if (action == RAISE) {
		const int street_bet = inactive_commit - prev.street_start_commit;
		int amount = prev.prev_act != RAISE ? std::max(1, prev.pot / 2) :
			std::max(static_cast<int>(street_bet * RAISE_AMT) -
				(active_commit - prev.street_start_commit), to_call + prev.last_raise);
		amount = std::min(amount, std::min(next_stack, to_call + inactive_stack));
		next.last_raise = amount - to_call;
		next_stack -= amount;
		next_commit += amount;
		next.pot += amount;
		next.raises++;
	}
	else if (action == PASS) {
		if (prev.prev_act == RAISE) {
			const int amount = std::min(to_call, next_stack);
			next_stack -= amount;
			next_commit += amount;
			next.pot += amount;
			const int refund = to_call - amount;
			auto& opponent_commit = prev.active == P1 ? next.p2commit : next.p1commit;
			auto& opponent_stack = prev.active == P1 ? next.p2stack : next.p1stack;
			opponent_commit -= refund;
			opponent_stack += refund;
			next.pot -= refund;
		}
		if (prev.prev_act == RAISE || prev.prev_act == PASS) {
			next.street++;
			next.prev_act = NONE;
			next.raises = 0;
			next.last_raise = 0;
			next.street_start_commit = next.p1commit;
			next.active = P1;
		}
	}

	return next;
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

	//we dont count prelfop towards commit
	const float initial_half_pot = (state.pot - state.p1commit - state.p2commit) / 2.0f;

	const int own_commit = traverser == P1 ? state.p1commit : state.p2commit;
	const int opponent_commit = traverser == P1 ? state.p2commit : state.p1commit;

	const float po = state.active == traverser
		? initial_half_pot + opponent_commit
		: -initial_half_pot - own_commit;

	for (int p1 = 0; p1 < reachp1.size(); p1++) {
		std::uint64_t p1h = Hands::get_mask_for_combo(p1); 
		for (int p2 = 0; p2 < reachp2.size(); p2++) {
			std::uint64_t p2h = Hands::get_mask_for_combo(p2);
			//we cant be blocking cards 
			if ((p1h & p2h) || ((p1h | p2h) & state.board))
				continue;
			if (traverser == P1)
				cf[p1] += po * reachp2[p2];
			else
				cf[p2] += po * reachp1[p1];
		}
	}

	return cf;
}

float Solver::payoff(GameState state, int p1, int p2, int trav) {
	float p = 0;
	//this assumes p1 perspective
	if (p1 > p2)
		p = state.pot / 2.0f;
	else if (p2 > p1)
		p = -state.pot / 2.0f;

	return trav == P1 ? p : -p;
}

/*
TODO - WE NEED TO DO CHIP CHECKING! 
 */
//check / call same value as they are the passive action
std::vector<int> Solver::get_legal_actions(const GameState& state) {
	if (state.prev_act == FOLD || state.street > RIVER)
		return {};
	const int active_commit = state.active == P1 ? state.p1commit : state.p2commit;
	const int inactive_commit = state.active == P1 ? state.p2commit : state.p1commit;
	const int active_stack = state.active == P1 ? state.p1stack : state.p2stack;
	const int inactive_stack = state.active == P1 ? state.p2stack : state.p1stack;
	const int to_call = std::max(0, inactive_commit - active_commit);
	if (to_call == 0 && (active_stack == 0 || inactive_stack == 0))
		return {};

	std::vector<int> actions{PASS};
	if (to_call > 0)
		actions.push_back(FOLD);
	if (state.raises < MAX_RAISES && active_stack > to_call && inactive_stack > 0)
		actions.push_back(RAISE);
	return actions;
}

//we should store regret as [actions][combos]
//each action[] has an infoset for a given combo
//note that combos is a less generalized form of "infoset"
std::vector<float> Solver::get_strategy(int legal_actions, int num_combos, const std::vector<double>& regret) {
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
