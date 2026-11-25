#include "hands.hpp"
#include "solver.hpp"
#include <numeric>
#include <stdexcept>

// Facing an all-in on the river leaves exactly two actions at a single node.
static void check_compact_storage(const std::array<turbofire::Card, 3>& flop, GameState state) {
    state.street = RIVER;
    state.board |= Hands::card_bit(35);
    state.prev_act = RAISE;
    state.p2commit = 20;
    state.p2stack = 0;
    state.pot = 60;

    std::vector<float> hero(1326), villain(1326);
    constexpr int hero_combo = 35 * (103 - 35) / 2; // Cards 35 and 36.
    hero[hero_combo] = 1; // Blocked by the river.
    villain.back() = 1;
    Solver solver(flop, hero, villain, true);
    auto check = [&](int in_range) {
        const auto values = solver.train(state, 10, 3);
        constexpr int legal_hands = 47 * 46 / 2;
        const auto bytes = 2 * (legal_hands + in_range) * sizeof(float);
        if (solver.action_nodes != 1 || solver.strategy_bytes != bytes)
            throw std::runtime_error("Incorrect compact river allocation");
        if (values.size() != 1326 || values[0] != 0 ||
            std::none_of(values.begin(), values.end(), [](float v) { return v != 0; }))
            throw std::runtime_error("Expected values for board-compatible hands outside the range");
    };
    check(0);
    check(0); // Continuing the solve must reuse its allocations.

    // Unblock hero's combo and reuse the solver with a different starting board.
    state.board ^= Hands::card_bit(35) | Hands::card_bit(15);
    check(1);
}

static void check_scenario_ranges() {
    const auto scenario = std::filesystem::path(TURBOFIRE_RANGE_DIR) / "hu_bb_vs_sb";
    const auto bet = Hands::load_range(scenario / "bb_3bet.csv");
    const auto call = Hands::load_range(scenario / "sb_call_3bet.csv");
    for (const auto* range : {&bet, &call}) {
        if (std::abs(std::accumulate(range->begin(), range->end(), 0.0) - 265.2) > 1e-5)
            throw std::runtime_error("Expected a 20% starting range");
    }
    const auto four_bet = Hands::parse_range_to_combos(Hands::parse_range({
        {"QQ+", 1}, {"AKs", 1}, {"AKo", 1}, {"A5s", 1}, {"A4s", 1}
    }));
    const auto dead = Hands::card_bit(12);
    const auto blocked = Hands::load_range(scenario / "sb_call_3bet.csv", dead);
    for (int i = 0; i < 1326; ++i) {
        if (four_bet[i] && call[i])
            throw std::runtime_error("Four-bet hand in call range");
        if (blocked[i] != ((Hands::get_mask_for_combo(i) & dead) ? 0 : call[i]))
            throw std::runtime_error("Loaded range has incorrect blocker weights");
    }
}

static void check_betting_config() {
    BettingConfig config{};
    config[TURN] = {{}, {1.1, 2, 3, 10}, 3};
    config[RIVER] = {{1, 0.25, 0.26, 8}, {}, 1};
    const std::array<turbofire::Card, 3> flop{0, 5, 9};
    std::vector<float> range(1326, 1);
    Solver solver(flop, range, range, true, config);
    GameState state{};
    state.prev_act = NONE;
    state.pot = 40;
    state.p1stack = state.p2stack = 80;
    auto check = [&](std::vector<int> expected) {
        std::vector<int> amounts;
        for (auto action : solver.get_legal_actions(state))
            if (action.kind == RAISE)
                amounts.push_back(action.chips);
        if (amounts != expected)
            throw std::runtime_error("Incorrect configured bet or raise amounts");
    };
    check({20}); // Default flop.
    state.street = TURN;
    check({});
    state.prev_act = RAISE;
    state.p2commit = state.last_raise = 10;
    state.raises = 1;
    check({20, 30, 80}); // Minimum raise and effective-stack limits.
    state.raises = 3;
    check({});

    state.street = RIVER;
    state.prev_act = NONE;
    state.p2commit = state.last_raise = state.raises = 0;
    for (int card : {0, 5, 9, 10, 35})
        state.board |= Hands::card_bit(card);
    check({10, 40, 80});
    solver.train(state, 10, 1);
    if (solver.action_nodes != 8)
        throw std::runtime_error("Expected three bet branches after either check opportunity");
    state.p1stack = 5;
    check({5});
    solver.train(state, 10, 1);
    if (solver.action_nodes != 4)
        throw std::runtime_error("Changed effective stack must rebuild the betting tree");
}

int main() {
    check_betting_config();
    check_scenario_ranges();
    const std::array<turbofire::Card, 3> flop{0, 5, 9};
    GameState state{};
    for (auto card : flop)
        state.board |= Hands::card_bit(card);
    state.prev_act = NONE;
    state.pot = 40;
    auto hero = Hands::parse_range_to_combos(Hands::parse_range(Hands::hero));
    auto villain = Hands::parse_range_to_combos(Hands::parse_range(Hands::villain));

    auto compare = [&](std::optional<turbofire::Card> turn, int iterations) {
        Solver full(flop, hero, villain, false);
        Solver shared(flop, hero, villain, true);
        auto expected = full.train(state, turn, iterations);
        auto actual = shared.train(state, turn, iterations);
        for (int i = 0; i < 1326; ++i) {
            if (!(std::abs(expected[i] - actual[i]) < 0.001f))
                throw std::runtime_error("Root counterfactual values differ at combo " + std::to_string(i));
            if ((Hands::get_mask_for_combo(i) & state.board) && actual[i] != 0)
                throw std::runtime_error("Board-blocked combo has nonzero value");
        }
        if (turn && shared.action_nodes >= full.action_nodes)
            throw std::runtime_error("Expected shared river subtrees");
    };

    compare(std::nullopt, 1);
    state.p1stack = state.p2stack = 80;
    state.street = TURN;
    state.board |= Hands::card_bit(10);
    compare(10, 3);
    villain[67] = 0.25f;
    compare(10, 3);
    check_compact_storage(flop, state);
}
