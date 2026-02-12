#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include "card/hand.h"
#include "game/action.h"
#include "solver/solver.h"

namespace poker {
namespace ui {

// 13x13 hand grid component
class HandGrid {
public:
    HandGrid();
    
    // Update strategy data
    void updateStrategy(const std::vector<std::pair<uint32_t, Strategy>>& strategies);
    
    // Get the ftxui component
    ftxui::Component getComponent();
    
    // Get currently hovered/selected hand
    Hand getSelectedHand() const;
    
    // Set callback for hand selection
    void onSelect(std::function<void(Hand)> callback);
    
private:
    // Grid dimensions
    static constexpr int GRID_SIZE = 13;
    
    // Strategy data for each cell [row][col]
    struct CellData {
        float fold = 0.0f;
        float check = 0.0f;
        float call = 0.0f;
        float bet = 0.0f;
        float raise = 0.0f;
        bool has_data = false;
    };
    std::array<std::array<CellData, GRID_SIZE>, GRID_SIZE> cells_;
    
    // Currently selected cell
    int selected_row_ = 0;
    int selected_col_ = 0;
    
    // Callback
    std::function<void(Hand)> on_select_;
    
    // Build grid element
    ftxui::Element RenderGrid();
    
    // Convert grid position to hand
    Hand gridToHand(int row, int col) const;
    
    // Get cell background color
    ftxui::Color getCellColor(int row, int col) const;
    
    // Get cell text color (for contrast)
    ftxui::Color getCellTextColor(int row, int col) const;
    
    // Render pie chart for strategy
    ftxui::Element renderPieChart(const CellData& data) const;
};

} // namespace ui
} // namespace poker
