#include "solver.hpp"
#include "hands.hpp"
#include "evaluator.hpp"
#include <stdexcept>

using turbofire::Card;

Solver::Solver(std::array<Card,3> flop, std::vector<float> hero, std::vector<float> villain, bool share, BettingConfig config)
    : betting(std::move(config)), ranges{std::move(hero), std::move(villain)} {
    for (const auto& street : betting) {
        if (street.raise_cap < 0)
            throw std::invalid_argument("Raise cap must be nonnegative");
        for (double size : street.bet_pot_fractions)
            if (!std::isfinite(size) || size <= 0)
                throw std::invalid_argument("Bet fractions must be finite and positive");
        for (double size : street.raise_to_multipliers)
            if (!std::isfinite(size) || size <= 1)
                throw std::invalid_argument("Raise-to multipliers must be finite and greater than one");
    }
    turbofire::ChanceIsomorphism iso(flop, ranges[0], ranges[1]);
    for (int i = 0, a = 0; a < 52; ++a) {
        for (int b = a + 1; b < 52; ++b, ++i) {
            cards[i] = {a,b};
            masks[i] = Hands::card_bit(a) | Hands::card_bit(b);
        }
    }
    const turbofire::SuitPermutation identity{0,1,2,3};
    for (int turn = -1; turn < 52; ++turn) {
        if (std::find(flop.begin(), flop.end(), turn) != flop.end())
            continue;
        std::map<Card, std::vector<turbofire::SuitPermutation>> grouped;
        for (const auto& deal : iso.deals(turn < 0 ? std::nullopt : std::optional<Card>(turn))) {
            auto perm = share ? deal.to_representative : identity;
            grouped[share ? deal.representative : deal.card].push_back(perm);
            if (permutations.count(perm))
                continue;
            auto& mapping = permutations[perm];
            for (int i = 0; i < 1326; ++i) {
                int a = turbofire::permute_card(cards[i][0], perm);
                int b = turbofire::permute_card(cards[i][1], perm);
                if (a > b)
                    std::swap(a,b);
                mapping[i] = a * (103 - a) / 2 + b - a - 1;
            }
        }
        for (auto& [card, mappings] : grouped)
            groups[turn].push_back({card, std::move(mappings)});
    }
}

const Solver::BoardHands& Solver::hands_on_board(std::uint64_t board) {
    auto [entry, inserted] = board_hands.try_emplace(board);
    auto& hands = entry->second;
    if (inserted) {
        for (int combo = 0; combo < 1326; ++combo) {
            if (masks[combo] & board)
                continue;
            int slot = static_cast<int>(hands.combos.size());
            hands.combos.push_back(combo);
            for (int player : {P1, P2}) {
                if (ranges[player][combo] > 0)
                    hands.in_range[player].push_back(slot);
            }
        }
    }
    return hands;
}

std::vector<float> Solver::train(GameState state, std::optional<Card> turn, int iterations) {
    // Reuse regrets only for the same starting tree.
    const auto layout = betting_root_key(state, turn);
    if (root_layout != layout) {
        root = Node{};
        action_nodes = 0;
        strategy_bytes = 0;
        root_layout = layout;
    }
    auto reach = ranges;
    for (auto& player : reach)
        for (int i = 0; i < 1326; ++i)
            if (masks[i] & state.board)
                player[i] = 0;
    std::vector<float> values;
    for (int i = 0; i < iterations; ++i) {
        values = solve(state, root, P1, reach, turn);
        solve(state, root, P2, reach, turn);
    }
    return values;
}

std::vector<float> Solver::solve(GameState state, Node& node, int traverser,
                                 const Reach& reach, std::optional<Card> turn) {
    if (state.prev_act == FOLD)
        return terminal(state, traverser, reach[1-traverser]);
    int count = __builtin_popcountll(state.board);
    if (count < 5 && (count < state.street + 3 ||
                      ((state.p1stack == 0 || state.p2stack == 0) && state.p1commit == state.p2commit)))
        return chance(state, node, traverser, reach, turn);
    auto actions = get_legal_actions(state);
    if (actions.empty())
        return terminal(state, traverser, reach[1-traverser]);
    const auto& hands = hands_on_board(state.board);
    const auto& in_range = hands.in_range[state.active];
    const auto legal_count = hands.combos.size();
    if (node.children.empty()) {
        node.children.resize(actions.size());
        node.regret_sum.resize(legal_count * actions.size());
        node.strategy_sum.resize(in_range.size() * actions.size());
        ++action_nodes;
        strategy_bytes += (node.regret_sum.size() + node.strategy_sum.size()) * sizeof(float);
    }
    auto strategy = get_strategy(actions.size(), legal_count, node.regret_sum);
    std::vector<std::vector<float>> child_values;
    std::vector<float> values(1326);
    for (std::size_t a = 0; a < actions.size(); ++a) {
        auto next = reach;
        const auto offset = a * legal_count;
        for (std::size_t slot = 0; slot < legal_count; ++slot)
            next[state.active][hands.combos[slot]] *= strategy[offset + slot];
        auto child = solve(next_state(state, actions[a]), node.children[a], traverser, next, turn);
        for (std::size_t slot = 0; slot < legal_count; ++slot) {
            int combo = hands.combos[slot];
            values[combo] += child[combo] * (traverser == state.active ? strategy[offset + slot] : 1.0f);
        }
        if (traverser == state.active)
            child_values.push_back(std::move(child));
    }
    if (traverser == state.active) {
        for (std::size_t a = 0; a < actions.size(); ++a) {
            const auto offset = a * legal_count;
            for (std::size_t slot = 0; slot < legal_count; ++slot) {
                int combo = hands.combos[slot];
                node.regret_sum[offset + slot] += child_values[a][combo] - values[combo];
            }
            for (std::size_t j = 0; j < in_range.size(); ++j) {
                int slot = in_range[j];
                node.strategy_sum[a * in_range.size() + j] += reach[traverser][hands.combos[slot]] * strategy[offset + slot];
            }
        }
    }
    return values;
}

std::vector<float> Solver::chance(GameState state, Node& node, int traverser,
                                  const Reach& reach, std::optional<Card> turn) {
    const auto& deals = groups.at(turn ? static_cast<int>(*turn) : -1);
    if (node.children.empty())
        node.children.resize(deals.size());
    std::vector<double> sum(1326);
    const double denominator = turn ? 44.0 : 45.0;
    for (std::size_t index = 0; index < deals.size(); ++index) {
        const auto& deal = deals[index];
        auto next = state;
        next.board |= Hands::card_bit(deal.card);
        auto next_reach = reach;
        for (auto& player : next_reach)
            for (int i = 0; i < 1326; ++i)
                player[i] = (masks[i] & next.board) ? 0 : player[i] / denominator;
        auto child = solve(next, node.children[index], traverser, next_reach, turn ? turn : std::optional<Card>(deal.card));
        for (const auto& permutation : deal.mappings) {
            const auto& mapping = permutations.at(permutation);
            for (int i = 0; i < 1326; ++i)
                sum[i] += child[mapping[i]];
        }
    }
    return {sum.begin(), sum.end()};
}

std::vector<float> Solver::terminal(GameState state, int traverser, const std::vector<float>& opponent) {
    std::vector<float> values(1326);
    struct Mass {
        double total = 0;
        std::array<double,52> blocked{};
        void add(const std::array<int,2>& hand, double weight) {
            total += weight;
            for (auto card : hand)
                blocked[card] += weight;
        }
        double compatible(const std::array<int,2>& hand) const {
            return total - blocked[hand[0]] - blocked[hand[1]];
        }
    };
    Mass all;
    for (int i = 0; i < 1326; ++i)
        all.add(cards[i], opponent[i]);
    if (state.prev_act == FOLD) {
        double initial = (state.pot - state.p1commit - state.p2commit) * 0.5;
        int own = traverser == P1 ? state.p1commit : state.p2commit;
        int other = traverser == P1 ? state.p2commit : state.p1commit;
        double payoff = state.active == traverser ? initial + other : -initial - own;
        for (int i = 0; i < 1326; ++i)
            if (!(masks[i] & state.board))
                values[i] = payoff * (all.compatible(cards[i]) + opponent[i]);
        return values;
    }
    auto& order = rankings[state.board];
    if (order.empty()) {
        for (int i = 0; i < 1326; ++i)
            if (!(masks[i] & state.board))
                order.emplace_back(evaluate_board(masks[i], state.board), i);
        std::sort(order.begin(), order.end());
    }
    Mass weaker;
    for (std::size_t begin = 0; begin < order.size();) {
        std::size_t end = begin;
        Mass tie;
        while (end < order.size() && order[end].first == order[begin].first) {
            int i = order[end++].second;
            tie.add(cards[i], opponent[i]);
        }
        for (auto j = begin; j < end; ++j) {
            int i = order[j].second;
            double net = 2 * weaker.compatible(cards[i]) + tie.compatible(cards[i]) - all.compatible(cards[i]);
            values[i] = net * state.pot * 0.5;
        }
        for (auto j = begin; j < end; ++j) {
            int i = order[j].second;
            weaker.add(cards[i], opponent[i]);
        }
        begin = end;
    }
    return values;
}
GameState Solver::next_state(GameState prev, BettingAction action) {
	auto next = prev;
	const int active_commit = prev.active == P1 ? prev.p1commit : prev.p2commit;
	const int inactive_commit = prev.active == P1 ? prev.p2commit : prev.p1commit;
	auto& next_commit = prev.active == P1 ? next.p1commit : next.p2commit;
	auto& next_stack = prev.active == P1 ? next.p1stack : next.p2stack;

	next.active = prev.active == P1 ? P2 : P1;
	next.prev_act = action.kind;
	const int to_call = std::max(0, inactive_commit - active_commit);

	if (action.kind == RAISE) {
		const int amount = action.chips;
		next.last_raise = amount - to_call;
		next_stack -= amount;
		next_commit += amount;
		next.pot += amount;
		next.raises++;
	}
	else if (action.kind == PASS) {
		if (to_call > 0) {
			const int amount = action.chips;
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

std::vector<BettingAction> Solver::get_legal_actions(const GameState& state) const {
	if (state.prev_act == FOLD || state.street > RIVER)
		return {};
	const int active_commit = state.active == P1 ? state.p1commit : state.p2commit;
	const int inactive_commit = state.active == P1 ? state.p2commit : state.p1commit;
	const int active_stack = state.active == P1 ? state.p1stack : state.p2stack;
	const int inactive_stack = state.active == P1 ? state.p2stack : state.p1stack;
	const int to_call = std::max(0, inactive_commit - active_commit);
	if (to_call == 0 && (active_stack == 0 || inactive_stack == 0))
		return {};

	std::vector<BettingAction> actions{{PASS, std::min(to_call, active_stack)}};
	if (to_call > 0)
		actions.push_back({FOLD, 0});
	const auto& rules = betting.at(state.street);
	if (state.raises >= rules.raise_cap || active_stack <= to_call || inactive_stack == 0)
		return actions;

	const int maximum = to_call + std::min(active_stack - to_call, inactive_stack);
	const auto& sizes = to_call ? rules.raise_to_multipliers : rules.bet_pot_fractions;
	for (double size : sizes) {
		double chips = size * state.pot;
		if (to_call) {
			chips = size * (inactive_commit - state.street_start_commit)
			        - (active_commit - state.street_start_commit);
			chips = std::max(chips, double(to_call) + std::max(1, state.last_raise));
		}
		int amount = static_cast<int>(std::clamp(chips, 1.0, double(maximum)));
		actions.push_back({RAISE, amount});
	}
	auto first_raise = actions.begin() + (to_call ? 2 : 1);
	std::sort(first_raise, actions.end(), [](auto a, auto b) { return a.chips < b.chips; });
	actions.erase(std::unique(first_raise, actions.end(), [](auto a, auto b) {
		return a.chips == b.chips;
	}), actions.end());
	return actions;
}

std::vector<float> Solver::get_strategy(std::size_t legal_actions, std::size_t num_combos, const std::vector<float>& regret) {
	std::vector<float> strategy(legal_actions * num_combos, 0.0);
	std::vector<float> rsum(num_combos, 0.0);

	for (std::size_t i = 0; i < legal_actions; i++) {
		for (std::size_t j = 0; j < num_combos; j++) {
			auto regret_i = (i*num_combos) + j;
			float r = std::max(0.0f, regret[regret_i]);
			strategy[regret_i] = r;
			rsum[j] += r;
		}
	}

	const float normalized_strat = 1.0 / legal_actions;

	for (std::size_t i = 0; i < legal_actions; i++) {
		for (std::size_t j = 0; j < num_combos; j++) {
			auto regret_i = (num_combos * i) + j;
			float rsum_val = rsum[j];
			if (rsum_val <= 0)
				strategy[regret_i] = normalized_strat;
			else
				strategy[regret_i] = strategy[regret_i] / rsum_val;
		}
	}

	return strategy;
}
