#include "solver.hpp"
#include "hands.hpp"
#include <charconv>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc > 4)
            throw std::invalid_argument("Usage: turbofire [flop[turn[river]]] [shared|full] [iterations]");
        std::string board = argc > 1 ? argv[1] : "2s7sJsQs";
        std::string mode = argc > 2 ? argv[2] : "shared";
        if (mode != "shared" && mode != "full")
            throw std::invalid_argument("Expected shared or full");
        if (board.size() != 6 && board.size() != 8 && board.size() != 10)
            throw std::invalid_argument("Expected three to five cards such as 2s7sJsQs");
        int iterations = 1;
        if (argc > 3) {
            std::string arg = argv[3];
            auto parsed = std::from_chars(arg.data(), arg.data() + arg.size(), iterations);
            if (parsed.ec != std::errc{} || parsed.ptr != arg.data() + arg.size() || iterations < 1)
                throw std::invalid_argument("Expected positive iteration count");
        }
        std::array<turbofire::Card,3> flop{};
        std::optional<turbofire::Card> turn;
        GameState state{};
        state.prev_act = NONE;
        state.street = board.size() / 2 - 3;
        state.pot = 40;
        state.p1stack = state.p2stack = 80;
        for (std::size_t i = 0; i < board.size(); i += 2) {
            auto rank = std::string("23456789TJQKA").find(board[i]);
            auto suit = std::string("shdc").find(board[i+1]);
            if (rank == std::string::npos || suit == std::string::npos)
                throw std::invalid_argument("Invalid card");
            turbofire::Card card = 13 * suit + rank;
            if (state.board & Hands::card_bit(card))
                throw std::invalid_argument("Duplicate board card");
            state.board |= Hands::card_bit(card);
            if (i < 6)
                flop[i/2] = card;
            if (i == 6)
                turn = card;
        }
        auto hero = Hands::parse_range_to_combos(Hands::parse_range(Hands::hero), state.board);
        auto villain = Hands::parse_range_to_combos(Hands::parse_range(Hands::villain), state.board);
        Solver solver(flop, hero, villain, mode == "shared");
        auto values = solver.train(state, turn, iterations);
        double utility = 0;
        double mass = 0;
        for (int i = 0; i < 1326; ++i) {
            utility += hero[i] * values[i];
            if (hero[i] == 0)
                continue;
            for (int j = 0; j < 1326; ++j)
                if (!(Hands::get_mask_for_combo(i) & Hands::get_mask_for_combo(j)))
                    mass += hero[i] * villain[j];
        }
        if (mass == 0)
            throw std::invalid_argument("No compatible hands in the ranges");
        std::cout << "action_nodes=" << solver.action_nodes << " strategy_bytes=" << solver.strategy_bytes
                  << " hero_ev=" << std::setprecision(10) << utility / mass << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
