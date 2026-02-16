#include <iostream>
#include <memory>
#include <vector>
#include "core/game_state.hpp"
#include "core/hand_evaluator.hpp"
#include "core/ranges.hpp"
#include "tree/tree_node.hpp"
#include "tree/tree_builder.hpp"
#include "solver/cfr_engine.hpp"
#include "ui/range_display.hpp"

// ftxui includes
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

using namespace gto;
using namespace ftxui;

template<size_t N>
void print_range_matrix(const std::array<float, N>& range) {
    constexpr std::array<const char*, 13> ranks = {
        "A", "K", "Q", "J", "T", "9", "8", "7", "6", "5", "4", "3", "2"
    };
    
    std::cout << "Range Matrix:\n";
    std::cout << "    ";
    for (int c = 0; c < 13; ++c) {
        std::cout << ranks[c] << "   ";
    }
    std::cout << "\n";
    
    for (int r = 0; r < 13; ++r) {
        std::cout << ranks[r] << " |";
        for (int c = 0; c < 13; ++c) {
            int idx;
            if (r == c) {
                idx = r * 13 + c;
            } else if (r < c) {
                idx = r * 13 + c;  // Suited
            } else {
                idx = c * 13 + r;  // Offsuit
            }
            
            float freq = range[idx];
            if (freq > 0.99f) {
                std::cout << "### ";
            } else if (freq > 0.5f) {
                std::cout << "##  ";
            } else if (freq > 0.0f) {
                std::cout << "#   ";
            } else {
                std::cout << ".   ";
            }
        }
        std::cout << "\n";
    }
}

int main(int argc, char* argv[]) {
    std::cout << "GTO Poker Solver - High Performance CFR+\n";
    std::cout << "========================================\n\n";
    
    // Show preflop ranges
    std::cout << "SB (BTN) Opening Range:\n";
    auto sb_range = PreflopRanges::get_sb_opening_range();
    print_range_matrix(sb_range);
    std::cout << "\n";
    
    std::cout << "BB Defending Range:\n";
    auto bb_range = PreflopRanges::get_bb_defend_range();
    print_range_matrix(bb_range);
    std::cout << "\n";
    
    // Phase 1: Build game tree
    std::cout << "Phase 1: Building game tree...\n";
    TreeBuilder::BuildConfig build_config;
    build_config.max_nodes = 1000000;  // 1M nodes for demo
    
    TreeBuilder builder(build_config);
    auto tree = builder.build_preflop_only();
    
    std::cout << "Tree built with " << tree.storage.size() << " nodes.\n\n";
    
    // Phase 2: Run CFR+ solver
    std::cout << "Phase 2: Running CFR+ solver...\n";
    CFREngine::CFRConfig cfr_config;
    cfr_config.num_iterations = 1000;  // Reduced for demo speed
    
    CFREngine solver(cfr_config);
    solver.solve(tree);
    
    std::cout << "\nSolver complete!\n";
    
    // Get strategy at root
    if (tree.root_idx >= 0) {
        auto strategy = solver.get_strategy(tree, tree.root_idx);
        std::cout << "Root node strategy computed for " << strategy.size() / 169 
                  << " actions x 169 hands.\n";
    }
    
    // Launch TUI if requested
    if (argc > 1 && std::string(argv[1]) == "--tui") {
        auto screen = ScreenInteractive::Fullscreen();
        
        // Create sample display data
        std::vector<float> fold_freqs(169, 0.0f);
        std::vector<float> call_freqs(169, 0.0f);
        std::vector<float> bet_freqs(169, 0.0f);
        
        // Initialize with some sample frequencies
        for (size_t i = 0; i < 169; ++i) {
            if (i % 3 == 0) {
                fold_freqs[i] = 0.3f;
                call_freqs[i] = 0.5f;
                bet_freqs[i] = 0.2f;
            } else if (i % 3 == 1) {
                fold_freqs[i] = 0.1f;
                call_freqs[i] = 0.3f;
                bet_freqs[i] = 0.6f;
            } else {
                fold_freqs[i] = 0.5f;
                call_freqs[i] = 0.4f;
                bet_freqs[i] = 0.1f;
            }
        }
        
        auto grid = RenderRangeGrid(fold_freqs, call_freqs, bet_freqs);
        auto legend = RenderLegend();
        
        auto renderer = Renderer([&] {
            return vbox({
                text("GTO Poker Solver - Strategy View") | bold | center,
                separator(),
                grid,
                separator(),
                legend,
                text("Press 'q' to quit") | center | color(Color::GrayDark)
            }) | border;
        });
        
        auto component = CatchEvent(renderer, [&](Event event) {
            if (event == Event::Character('q')) {
                screen.ExitLoopClosure()();
                return true;
            }
            return false;
        });
        
        screen.Loop(component);
    } else {
        std::cout << "\nRun with --tui flag to launch the terminal UI.\n";
    }
    
    return 0;
}
