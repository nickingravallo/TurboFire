#include "tui/grid.h"
#include "tui/colors.h"
#include <algorithm>

namespace poker {
namespace ui {

HandGrid::HandGrid() {
    cells_.fill({});
}

void HandGrid::updateStrategy(const std::vector<std::pair<uint32_t, Strategy>>& strategies) {
    // Clear existing data
    for (auto& row : cells_) {
        for (auto& cell : row) {
            cell = CellData{};
        }
    }
    
    // Fill ALL 169 cells with default data first
    // Pairs (diagonal)
    for (int rank = 0; rank < 13; ++rank) {
        int row = 12 - rank;
        cells_[row][row].has_data = true;
        cells_[row][row].check = 0.5f;
        cells_[row][row].call = 0.3f;
        cells_[row][row].bet = 0.2f;
    }
    
    // Suited (above diagonal)
    for (int rh = 12; rh >= 1; --rh) {
        for (int rl = rh - 1; rl >= 0; --rl) {
            int row = 12 - rh;
            int col = 12 - rl;
            cells_[row][col].has_data = true;
            cells_[row][col].check = 0.4f;
            cells_[row][col].call = 0.35f;
            cells_[row][col].bet = 0.25f;
        }
    }
    
    // Offsuit (below diagonal)
    for (int rh = 12; rh >= 1; --rh) {
        for (int rl = rh - 1; rl >= 0; --rl) {
            int row = 12 - rl;
            int col = 12 - rh;
            cells_[row][col].has_data = true;
            cells_[row][col].check = 0.45f;
            cells_[row][col].call = 0.3f;
            cells_[row][col].fold = 0.25f;
        }
    }
    
    // Now overlay actual strategy data where available
    // Use a simple mapping: hand_idx % 169 gives hand type 0-168
    for (const auto& [hand_idx, strategy] : strategies) {
        if (strategy.probabilities.empty()) continue;
        
        int hand_type = hand_idx % 169;
        
        // Map hand_type 0-168 to grid position
        int row, col;
        if (hand_type < 13) {
            // Pairs: 0-12 -> AA, KK, ..., 22
            row = 12 - hand_type;
            col = row;
        } else if (hand_type < 91) {
            // Suited: 13-90
            int suited_idx = hand_type - 13;
            int count = 0;
            bool found = false;
            for (int rh = 12; rh >= 1 && !found; --rh) {
                for (int rl = rh - 1; rl >= 0 && !found; --rl) {
                    if (count == suited_idx) {
                        row = 12 - rh;
                        col = 12 - rl;
                        found = true;
                    }
                    count++;
                }
            }
            if (!found) continue;
        } else {
            // Offsuit: 91-168
            int offsuit_idx = hand_type - 91;
            int count = 0;
            bool found = false;
            for (int rh = 12; rh >= 1 && !found; --rh) {
                for (int rl = rh - 1; rl >= 0 && !found; --rl) {
                    if (count == offsuit_idx) {
                        row = 12 - rl;
                        col = 12 - rh;
                        found = true;
                    }
                    count++;
                }
            }
            if (!found) continue;
        }
        
        if (row >= 0 && row < GRID_SIZE && col >= 0 && col < GRID_SIZE) {
            CellData& cell = cells_[row][col];
            cell.has_data = true;
            if (strategy.probabilities.size() > 0) cell.fold = strategy.probabilities[0];
            if (strategy.probabilities.size() > 1) cell.check = strategy.probabilities[1];
            if (strategy.probabilities.size() > 2) cell.call = strategy.probabilities[2];
            if (strategy.probabilities.size() > 3) cell.bet = strategy.probabilities[3];
            if (strategy.probabilities.size() > 4) cell.raise = strategy.probabilities[4];
        }
    }
}

ftxui::Component HandGrid::getComponent() {
    return ftxui::Renderer([this] {
        return RenderGrid();
    }) | ftxui::CatchEvent([this](ftxui::Event event) {
        if (event == ftxui::Event::ArrowUp) {
            selected_row_ = std::max(0, selected_row_ - 1);
            return true;
        }
        if (event == ftxui::Event::ArrowDown) {
            selected_row_ = std::min(GRID_SIZE - 1, selected_row_ + 1);
            return true;
        }
        if (event == ftxui::Event::ArrowLeft) {
            selected_col_ = std::max(0, selected_col_ - 1);
            return true;
        }
        if (event == ftxui::Event::ArrowRight) {
            selected_col_ = std::min(GRID_SIZE - 1, selected_col_ + 1);
            return true;
        }
        if (event == ftxui::Event::Return) {
            if (on_select_) {
                on_select_(gridToHand(selected_row_, selected_col_));
            }
            return true;
        }
        return false;
    });
}

Hand HandGrid::getSelectedHand() const {
    return gridToHand(selected_row_, selected_col_);
}

void HandGrid::onSelect(std::function<void(Hand)> callback) {
    on_select_ = callback;
}

ftxui::Element HandGrid::RenderGrid() {
    std::vector<ftxui::Elements> grid_elements;
    
    // Header row
    ftxui::Elements header = {ftxui::text("   ")};
    for (int c = 0; c < GRID_SIZE; ++c) {
        char rank_char = Card::RANK_CHARS[12 - c];
        header.push_back(ftxui::text(std::string(1, rank_char)) | ftxui::center | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 3));
    }
    grid_elements.push_back(header);
    
    // Data rows
    for (int r = 0; r < GRID_SIZE; ++r) {
        ftxui::Elements row;
        
        // Row header
        char rank_char = Card::RANK_CHARS[12 - r];
        row.push_back(ftxui::text(std::string(1, rank_char)) | ftxui::center);
        
        // Cells
        for (int c = 0; c < GRID_SIZE; ++c) {
            ftxui::Color bg = getCellColor(r, c);
            ftxui::Color fg = getCellTextColor(r, c);
            
            Hand hand = gridToHand(r, c);
            std::string label = hand.toString();
            
            auto cell = ftxui::text(label) | ftxui::center | ftxui::color(fg);
            
            if (r == selected_row_ && c == selected_col_) {
                cell = cell | ftxui::bold | ftxui::border;
            }
            
            cell = cell | ftxui::bgcolor(bg);
            row.push_back(cell | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 5) | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 1));
        }
        
        grid_elements.push_back(row);
    }
    
    // Build table
    ftxui::Elements rows;
    for (const auto& row : grid_elements) {
        rows.push_back(ftxui::hbox(row));
    }
    
    return ftxui::vbox(rows) | ftxui::border;
}

Hand HandGrid::gridToHand(int row, int col) const {
    // Convert grid position to hand
    int r1 = 12 - row;
    int r2 = 12 - col;
    
    if (row == col) {
        // Pair
        return Hand(Card(r1, 0), Card(r1, 1));
    } else if (row < col) {
        // Suited (above diagonal)
        return Hand(Card(r1, 0), Card(r2, 0));
    } else {
        // Offsuit (below diagonal)
        return Hand(Card(r2, 0), Card(r1, 1));
    }
}

ftxui::Color HandGrid::getCellColor(int row, int col) const {
    const CellData& cell = cells_[row][col];
    
    if (!cell.has_data) {
        return COLOR_BG_MID;
    }
    
    return blendStrategyColors(cell.fold, cell.check, cell.call, cell.bet, cell.raise);
}

ftxui::Color HandGrid::getCellTextColor(int row, int col) const {
    // Always use white text for contrast
    return COLOR_TEXT_WHITE;
}

ftxui::Element HandGrid::renderPieChart(const CellData& data) const {
    // Simplified - just return a colored box
    ftxui::Color color = blendStrategyColors(data.fold, data.check, data.call, data.bet, data.raise);
    return ftxui::text(" ") | ftxui::bgcolor(color);
}

} // namespace ui
} // namespace poker
