#include <iostream>
#include "card/card.h"
#include "card/hand.h"

using namespace poker;

int main() {
    std::cout << "=== Testing Hand Index to Grid Mapping ===" << std::endl;
    
    std::cout << "\nChecking indices 70-85 (around AKs):" << std::endl;
    for (uint32_t idx = 70; idx < 86 && idx < 1326; ++idx) {
        Hand hand = HandRange::indexToHand(idx);
        int rank1 = hand.card1().rank();
        int rank2 = hand.card2().rank();
        
        std::cout << "Index " << idx << ": " << hand.toString() 
                  << " (ranks: " << (int)rank1 << "," << (int)rank2 << ") ";
        
        if (hand.isPair()) {
            int row = 12 - rank1;
            std::cout << "-> Pair at [" << row << "][" << row << "]";
        } else if (hand.isSuited()) {
            int row = 12 - std::max(rank1, rank2);
            int col = 12 - std::min(rank1, rank2);
            std::cout << "-> Suited at [" << row << "][" << col << "]";
        } else {
            int row = 12 - std::min(rank1, rank2);
            int col = 12 - std::max(rank1, rank2);
            std::cout << "-> Offsuit at [" << row << "][" << col << "]";
        }
        std::cout << std::endl;
    }
    
    std::cout << "\nVerifying all 169 grid cells get at least one hand:" << std::endl;
    bool grid[13][13] = {{false}};
    
    for (uint32_t idx = 0; idx < 1326; ++idx) {
        Hand hand = HandRange::indexToHand(idx);
        int rank1 = hand.card1().rank();
        int rank2 = hand.card2().rank();
        
        int row, col;
        if (hand.isPair()) {
            row = 12 - rank1;
            col = row;
        } else if (hand.isSuited()) {
            row = 12 - std::max(rank1, rank2);
            col = 12 - std::min(rank1, rank2);
        } else {
            row = 12 - std::min(rank1, rank2);
            col = 12 - std::max(rank1, rank2);
        }
        
        if (row >= 0 && row < 13 && col >= 0 && col < 13) {
            grid[row][col] = true;
        }
    }
    
    int covered = 0;
    for (int r = 0; r < 13; ++r) {
        for (int c = 0; c < 13; ++c) {
            if (grid[r][c]) covered++;
        }
    }
    std::cout << "Grid cells covered: " << covered << "/169" << std::endl;
    
    // Show which cells are NOT covered
    std::cout << "\nMissing cells (row, col):" << std::endl;
    for (int r = 0; r < 13; ++r) {
        for (int c = 0; c < 13; ++c) {
            if (!grid[r][c]) {
                std::cout << "  [" << r << "][" << c << "]" << std::endl;
            }
        }
    }
    
    return 0;
}
