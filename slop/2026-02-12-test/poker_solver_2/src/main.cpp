#include <iostream>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

#include "card/card.h"
#include "game/board.h"
#include "game/action.h"
#include "solver/solver.h"

using namespace poker;

// ANSI color codes for terminal output
namespace Color {
    const char* RESET = "\033[0m";
    const char* BOLD = "\033[1m";
    const char* FOLD = "\033[34m";      // Blue
    const char* CHECK = "\033[32m";     // Green
    const char* CALL = "\033[91m";      // Light red
    const char* BET = "\033[31m";       // Deep red
    const char* RAISE = "\033[35m";     // Magenta
    const char* BG_RED = "\033[41m";
    const char* BG_GREEN = "\033[42m";
    const char* BG_BLUE = "\033[44m";
    const char* BG_YELLOW = "\033[43m";
    const char* BG_MAGENTA = "\033[45m";
    const char* BG_CYAN = "\033[46m";
    const char* BG_WHITE = "\033[47m";
}

// Hand notation for 13x13 grid
std::string getHandNotation(int row, int col) {
    const char ranks[] = "AKQJT98765432";
    
    if (row == col) {
        // Pairs
        return std::string(1, ranks[row]) + std::string(1, ranks[row]) + " ";
    } else if (row < col) {
        // Suited
        return std::string(1, ranks[row]) + std::string(1, ranks[col]) + "s";
    } else {
        // Offsuit
        return std::string(1, ranks[col]) + std::string(1, ranks[row]) + "o";
    }
}

// Get color based on dominant action
std::string getStrategyColor(float fold, float check, float call, 
                             float bet_20, float bet_33, float bet_52, 
                             float bet_100, float bet_123, float raise) {
    float total = fold + check + call + bet_20 + bet_33 + bet_52 + bet_100 + bet_123 + raise;
    if (total < 0.01f) return Color::RESET;
    
    // Normalize
    fold /= total;
    check /= total;
    call /= total;
    float bet = (bet_20 + bet_33 + bet_52 + bet_100 + bet_123) / total;
    raise /= total;
    
    // Return color of dominant action
    if (fold >= check && fold >= call && fold >= bet && fold >= raise) {
        return Color::FOLD;
    } else if (check >= fold && check >= call && check >= bet && check >= raise) {
        return Color::CHECK;
    } else if (call >= fold && call >= check && call >= bet && call >= raise) {
        return Color::CALL;
    } else if (bet >= fold && bet >= check && bet >= call && bet >= raise) {
        return Color::BET;
    } else {
        return Color::RAISE;
    }
}

// Print 13x13 grid with strategies
void printGrid(const std::vector<std::pair<uint32_t, Strategy>>& strategies) {
    std::cout << "\n" << Color::BOLD << "13x13 Hand Strategy Grid" << Color::RESET << "\n";
    std::cout << "Legend: " << Color::FOLD << "Fold" << Color::RESET << " | " 
              << Color::CHECK << "Check" << Color::RESET << " | "
              << Color::CALL << "Call" << Color::RESET << " | "
              << Color::BET << "Bet" << Color::RESET << " | "
              << Color::RAISE << "Raise" << Color::RESET << "\n\n";
    
    // Header
    std::cout << "    ";
    const char ranks[] = "AKQJT98765432";
    for (int c = 0; c < 13; ++c) {
        std::cout << ranks[c] << "   ";
    }
    std::cout << "\n";
    
    // Grid rows
    for (int r = 0; r < 13; ++r) {
        std::cout << ranks[r] << " ";
        
        for (int c = 0; c < 13; ++c) {
            // Calculate hand type index (0-168)
            int hand_idx;
            if (r == c) {
                hand_idx = r;  // Pairs
            } else if (r < c) {
                // Suited: 13 + sum of decreasing sequence
                hand_idx = 13;
                for (int i = 0; i < r; ++i) {
                    hand_idx += (12 - i);
                }
                hand_idx += (c - r - 1);
            } else {
                // Offsuit: 91 + sum
                hand_idx = 91;
                for (int i = 0; i < c; ++i) {
                    hand_idx += (12 - i);
                }
                hand_idx += (r - c - 1);
            }
            
            // Find strategy for this hand
            std::string color = Color::RESET;
            for (const auto& [idx, strat] : strategies) {
                if (idx == static_cast<uint32_t>(hand_idx) && !strat.probabilities.empty()) {
                    // Map solver probabilities (8 actions) to our format
                    float fold = strat.probabilities[0];
                    float check = strat.probabilities[1];
                    float call = strat.probabilities[2];
                    float bet_20 = strat.probabilities[3];
                    float bet_33 = strat.probabilities[4];
                    float bet_52 = strat.probabilities[5];
                    float bet_100 = strat.probabilities[6];
                    float raise = strat.probabilities[7];
                    
                    color = getStrategyColor(fold, check, call, bet_20, bet_33, 
                                            bet_52, bet_100, 0.0f, raise);
                    break;
                }
            }
            
            std::cout << color << getHandNotation(r, c) << Color::RESET << " ";
        }
        std::cout << "\n";
    }
}

// Print detailed strategy for a specific hand
void printHandStrategy(uint32_t hand_idx, const Strategy& strat) {
    if (strat.probabilities.empty()) {
        std::cout << "No strategy data available\n";
        return;
    }
    
    const char* actions[] = {"Fold", "Check", "Call", "Bet 20%", "Bet 33%", 
                            "Bet 52%", "Bet 100%", "Raise"};
    
    std::cout << Color::BOLD << "Strategy Breakdown:" << Color::RESET << "\n";
    for (size_t i = 0; i < strat.probabilities.size() && i < 8; ++i) {
        float pct = strat.probabilities[i] * 100.0f;
        if (pct > 0.1f) {
            std::cout << "  " << std::setw(10) << std::left << actions[i] 
                     << ": " << std::fixed << std::setprecision(1) << pct << "%\n";
        }
    }
}

// Show usage
void printUsage(const char* program) {
    std::cout << "GTO Poker Solver - Command Line Version\n\n";
    std::cout << "Usage: " << program << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -b, --board <cards>      Board cards (e.g., AsKh7d)\n";
    std::cout << "  -i, --iterations <n>     Number of CFR iterations (default: 10000)\n";
    std::cout << "  -h, --hand <idx>         Show detailed strategy for hand index (0-168)\n";
    std::cout << "  --help                   Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program << " -b AsKh7d\n";
    std::cout << "  " << program << " --board Ts9s8s -i 5000\n";
    std::cout << "  " << program << " -b QhJd2c -h 0  # Show AA strategy\n";
}

int main(int argc, char* argv[]) {
    // Parse arguments
    std::string board_str;
    uint32_t iterations = 10000;
    int show_hand = -1;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if ((arg == "-b" || arg == "--board") && i + 1 < argc) {
            board_str = argv[++i];
        } else if ((arg == "-i" || arg == "--iterations") && i + 1 < argc) {
            iterations = std::stoi(argv[++i]);
        } else if ((arg == "-h" || arg == "--hand") && i + 1 < argc) {
            show_hand = std::stoi(argv[++i]);
        }
    }
    
    if (board_str.empty()) {
        std::cerr << "Error: Board cards required. Use -b or --board\n";
        printUsage(argv[0]);
        return 1;
    }
    
    // Parse board
    Board board;
    try {
        if (board_str.length() >= 6) {
            // Flop
            board.setFlop(
                Card::fromString(board_str.substr(0, 2)),
                Card::fromString(board_str.substr(2, 2)),
                Card::fromString(board_str.substr(4, 2))
            );
            
            // Turn
            if (board_str.length() >= 8) {
                board.setTurn(Card::fromString(board_str.substr(6, 2)));
            }
            
            // River
            if (board_str.length() >= 10) {
                board.setRiver(Card::fromString(board_str.substr(8, 2)));
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing board: " << e.what() << "\n";
        return 1;
    }
    
    // Print header
    std::cout << "\n" << Color::BOLD << "GTO Poker Solver" << Color::RESET << "\n";
    std::cout << std::string(50, '=') << "\n\n";
    
    std::cout << "Board: " << Color::BOLD << board.toString() << Color::RESET << "\n";
    std::cout << "Iterations: " << iterations << "\n\n";
    
    // Create and run solver
    Solver::Config config;
    config.iterations = iterations;
    config.save_solved = false;
    
    Solver solver(config);
    
    std::cout << "Running CFR solver...\n";
    auto start = std::chrono::high_resolution_clock::now();
    
    // Progress callback would go here - for now just run
    solver.solve(board, std::vector<Action>{});
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double>(end - start).count();
    
    std::cout << "Complete! Time: " << std::fixed << std::setprecision(2) 
              << duration << " seconds\n\n";
    
    // Get strategies
    auto strategies = solver.getAllStrategies();
    std::cout << "Retrieved strategies for " << strategies.size() << " hand types\n";
    
    // Print grid
    printGrid(strategies);
    
    // Show detailed strategy for specific hand if requested
    if (show_hand >= 0 && show_hand < static_cast<int>(strategies.size())) {
        std::cout << "\n" << Color::BOLD << "Hand Index " << show_hand 
                  << " Strategy:" << Color::RESET << "\n";
        printHandStrategy(strategies[show_hand].first, strategies[show_hand].second);
    }
    
    // Show summary of top hands
    std::cout << "\n" << Color::BOLD << "Top 5 Aggressive Hands:" << Color::RESET << "\n";
    std::vector<std::pair<uint32_t, float>> aggressive_scores;
    for (const auto& [idx, strat] : strategies) {
        if (strat.probabilities.size() >= 8) {
            // Calculate aggression score (bet + raise)
            float score = strat.probabilities[3] + strat.probabilities[4] + 
                         strat.probabilities[5] + strat.probabilities[6] + 
                         strat.probabilities[7];
            aggressive_scores.push_back({idx, score});
        }
    }
    
    // Sort by aggression
    std::sort(aggressive_scores.begin(), aggressive_scores.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    
    // Print top 5
    for (int i = 0; i < std::min(5, (int)aggressive_scores.size()); ++i) {
        uint32_t hand_idx = aggressive_scores[i].first;
        uint32_t hand_type = hand_idx % 169;  // Convert to hand type (0-168)
        
        // Convert hand_type (0-168) to grid position (row, col)
        int row, col;
        if (hand_type < 13) {
            // Pairs: 0-12 -> (0,0), (1,1), ... (12,12)
            row = col = hand_type;
        } else if (hand_type < 91) {
            // Suited: 13-90
            // Formula: hand_type = 13 + (sum from i=0 to row-1 of (12-i)) + (col-row-1)
            int suited_idx = hand_type - 13;
            row = 0;
            int count = 0;
            while (row < 12 && count + (12 - row) <= suited_idx) {
                count += (12 - row);
                row++;
            }
            col = row + 1 + (suited_idx - count);
        } else {
            // Offsuit: 91-168
            // Formula: hand_type = 91 + (sum from i=0 to col-1 of (12-i)) + (row-col-1)
            int offsuit_idx = hand_type - 91;
            col = 0;
            int count = 0;
            while (col < 12 && count + (12 - col) <= offsuit_idx) {
                count += (12 - col);
                col++;
            }
            row = col + 1 + (offsuit_idx - count);
        }
        
        std::cout << "  " << std::setw(3) << getHandNotation(row, col) 
                  << ": " << std::fixed << std::setprecision(1) 
                  << (aggressive_scores[i].second * 100) << "% aggressive\n";
    }
    
    std::cout << "\n";
    
    return 0;
}
