#include "hands.hpp"
#include "solver.hpp"
#include <stdexcept>

int main() {
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
}
