#pragma once

#include <array>
#include <string>
#include <functional>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

namespace poker {
namespace ui {

// Strategy data for one cell
struct CellStrategy {
    float fold = 0.0f;
    float check = 0.0f;
    float call = 0.0f;
    float bet_20 = 0.0f;
    float bet_33 = 0.0f;
    float bet_52 = 0.0f;
    float bet_100 = 0.0f;
    float bet_123 = 0.0f;
    float raise = 0.0f;
    
    // Get dominant action
    std::pair<std::string, float> getDominantAction() const;
    
    // Get color based on strategy mix
    ftxui::Color getColor() const;
};

// 169 hand types mapping
enum class HandType {
    PAIRS = 0,      // 0-12 (AA, KK, ..., 22)
    SUITED = 1,     // 13-90 (AKs, AQs, ..., 32s)
    OFFSUIT = 2     // 91-168 (AKo, AQo, ..., 32o)
};

// Grid cell state
struct GridCell {
    CellStrategy strategy;
    std::string notation;  // "AA", "AKs", "72o"
    bool is_hovered = false;
    bool is_selected = false;
    int row = 0;
    int col = 0;
    
    // Convert to hand type index (0-168)
    int getHandTypeIndex() const;
};

// 13×13 Hand Grid Component
class HandGrid {
public:
    static constexpr int GRID_SIZE = 13;
    static constexpr int NUM_HAND_TYPES = 169;
    
    HandGrid();
    
    // Initialize grid with hand notations
    void initialize();
    
    // Update strategy for a hand type (0-168)
    void updateStrategy(int hand_type, const CellStrategy& strategy);
    
    // Bulk update all strategies
    void updateAllStrategies(const std::vector<CellStrategy>& strategies);
    
    // Get the ftxui component
    ftxui::Component getComponent();
    
    // Get currently selected cell
    const GridCell* getSelectedCell() const;
    
    // Set callback for cell selection
    void onSelect(std::function<void(const GridCell&)> callback);
    
    // Set callback for hover
    void onHover(std::function<void(const GridCell&)> callback);
    
    // Clear selection
    void clearSelection();
    
private:
    std::array<std::array<GridCell, GRID_SIZE>, GRID_SIZE> cells_;
    int selected_row_ = -1;
    int selected_col_ = -1;
    int hovered_row_ = -1;
    int hovered_col_ = -1;
    
    std::function<void(const GridCell&)> on_select_;
    std::function<void(const GridCell&)> on_hover_;
    
    // Initialize hand notations
    void initializeNotations();
    
    // Render functions
    ftxui::Element RenderGrid();
    ftxui::Element RenderCell(int row, int col);
    ftxui::Element RenderTooltip(int row, int col);
    
    // Get cell color
    ftxui::Color GetCellBackground(int row, int col) const;
    ftxui::Color GetCellForeground(int row, int col) const;
};

} // namespace ui
} // namespace poker
