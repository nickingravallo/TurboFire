#include "components/grid.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace poker {
namespace ui {

// Color definitions (matching the user's requirements)
static const ftxui::Color COLOR_FOLD = ftxui::Color::Blue;
static const ftxui::Color COLOR_CHECK = ftxui::Color::RGB(0, 170, 0);  // Green
static const ftxui::Color COLOR_CALL = ftxui::Color::RGB(255, 107, 107);  // Light red
static const ftxui::Color COLOR_BET = ftxui::Color::RGB(204, 0, 0);  // Deep red
static const ftxui::Color COLOR_RAISE = ftxui::Color::RGB(139, 0, 0);  // Dark red
static const ftxui::Color COLOR_BG_DEFAULT = ftxui::Color::RGB(45, 45, 45);
static const ftxui::Color COLOR_BG_HIGHLIGHT = ftxui::Color::RGB(60, 60, 60);
static const ftxui::Color COLOR_TEXT = ftxui::Color::White;

std::pair<std::string, float> CellStrategy::getDominantAction() const {
    struct ActionData {
        std::string name;
        float value;
    };
    
    std::vector<ActionData> actions = {
        {"Fold", fold},
        {"Check", check},
        {"Call", call},
        {"Bet", std::max({bet_20, bet_33, bet_52, bet_100, bet_123})},
        {"Raise", raise}
    };
    
    auto max_it = std::max_element(actions.begin(), actions.end(),
        [](const ActionData& a, const ActionData& b) { return a.value < b.value; });
    
    return {max_it->name, max_it->value};
}

ftxui::Color CellStrategy::getColor() const {
    // Blend colors based on strategy mix
    float total = fold + check + call + bet_20 + bet_33 + bet_52 + bet_100 + bet_123 + raise;
    if (total < 0.01f) return COLOR_BG_DEFAULT;
    
    // Normalize
    float f = fold / total;
    float c = check / total;
    float ca = call / total;
    float b = (bet_20 + bet_33 + bet_52 + bet_100 + bet_123) / total;
    float r = raise / total;
    
    // Simple blending - use dominant action color with intensity
    if (f >= c && f >= ca && f >= b && f >= r) {
        // Blue for fold
        uint8_t intensity = static_cast<uint8_t>(50 + f * 150);
        return ftxui::Color::RGB(0, 0, intensity);
    } else if (c >= f && c >= ca && c >= b && c >= r) {
        // Green for check
        uint8_t intensity = static_cast<uint8_t>(50 + c * 150);
        return ftxui::Color::RGB(0, intensity, 0);
    } else if (ca >= f && ca >= c && ca >= b && ca >= r) {
        // Light red for call
        uint8_t intensity = static_cast<uint8_t>(150 + ca * 80);
        return ftxui::Color::RGB(intensity, intensity/2, intensity/2);
    } else if (b >= f && b >= c && b >= ca && b >= r) {
        // Deep red for bet
        uint8_t intensity = static_cast<uint8_t>(150 + b * 80);
        return ftxui::Color::RGB(intensity, 0, 0);
    } else {
        // Dark red for raise
        uint8_t intensity = static_cast<uint8_t>(100 + r * 100);
        return ftxui::Color::RGB(intensity, 0, 0);
    }
}

int GridCell::getHandTypeIndex() const {
    if (row == col) {
        // Pairs: diagonal, 0-12
        return row;
    } else if (row < col) {
        // Suited: above diagonal, 13-90
        int count = 13;
        for (int r = 0; r < row; ++r) {
            count += (12 - r);
        }
        count += (col - row - 1);
        return count;
    } else {
        // Offsuit: below diagonal, 91-168
        int count = 91;
        for (int c = 0; c < col; ++c) {
            count += (12 - c);
        }
        count += (row - col - 1);
        return count;
    }
}

HandGrid::HandGrid() {
    initialize();
}

void HandGrid::initialize() {
    initializeNotations();
    
    // Set default strategies (before solver runs)
    for (int r = 0; r < GRID_SIZE; ++r) {
        for (int c = 0; c < GRID_SIZE; ++c) {
            cells_[r][c].row = r;
            cells_[r][c].col = c;
            
            // Default strategies based on hand strength
            int hand_type = cells_[r][c].getHandTypeIndex();
            
            if (r == c) {
                // Pairs - play more aggressively
                cells_[r][c].strategy.check = 0.2f;
                cells_[r][c].strategy.bet_33 = 0.5f;
                cells_[r][c].strategy.bet_100 = 0.2f;
                cells_[r][c].strategy.call = 0.1f;
            } else if (r < c) {
                // Suited - moderately aggressive
                cells_[r][c].strategy.check = 0.4f;
                cells_[r][c].strategy.bet_33 = 0.3f;
                cells_[r][c].strategy.call = 0.2f;
                cells_[r][c].strategy.fold = 0.1f;
            } else {
                // Offsuit - more passive
                cells_[r][c].strategy.check = 0.5f;
                cells_[r][c].strategy.fold = 0.2f;
                cells_[r][c].strategy.call = 0.2f;
                cells_[r][c].strategy.bet_33 = 0.1f;
            }
        }
    }
}

void HandGrid::initializeNotations() {
    const char ranks[] = "AKQJT98765432";
    
    for (int r = 0; r < GRID_SIZE; ++r) {
        for (int c = 0; c < GRID_SIZE; ++c) {
            if (r == c) {
                // Pairs
                cells_[r][c].notation = std::string(1, ranks[r]) + std::string(1, ranks[r]);
            } else if (r < c) {
                // Suited: row index < column index means row card is HIGHER (A at 0, K at 1, etc.)
                // Example: r=0 (A), c=1 (K) -> "AKs"
                cells_[r][c].notation = std::string(1, ranks[r]) + std::string(1, ranks[c]) + "s";
            } else {
                // Offsuit: row index > column index means column card is HIGHER
                // Example: r=5 (T), c=0 (A) -> "ATo" (A is higher than T)
                cells_[r][c].notation = std::string(1, ranks[c]) + std::string(1, ranks[r]) + "o";
            }
        }
    }
}

void HandGrid::updateStrategy(int hand_type, const CellStrategy& strategy) {
    // Find which cell corresponds to this hand type
    for (int r = 0; r < GRID_SIZE; ++r) {
        for (int c = 0; c < GRID_SIZE; ++c) {
            if (cells_[r][c].getHandTypeIndex() == hand_type) {
                cells_[r][c].strategy = strategy;
                return;
            }
        }
    }
}

void HandGrid::updateAllStrategies(const std::vector<CellStrategy>& strategies) {
    if (strategies.size() != NUM_HAND_TYPES) return;
    
    for (int i = 0; i < NUM_HAND_TYPES; ++i) {
        updateStrategy(i, strategies[i]);
    }
}

ftxui::Component HandGrid::getComponent() {
    auto component = ftxui::Renderer([this] {
        return RenderGrid();
    });
    
    component |= ftxui::CatchEvent([this](ftxui::Event event) {
        if (event == ftxui::Event::ArrowUp) {
            if (selected_row_ > 0) {
                selected_row_--;
                if (on_select_) on_select_(cells_[selected_row_][selected_col_]);
            }
            return true;
        }
        if (event == ftxui::Event::ArrowDown) {
            if (selected_row_ < GRID_SIZE - 1) {
                selected_row_++;
                if (on_select_) on_select_(cells_[selected_row_][selected_col_]);
            }
            return true;
        }
        if (event == ftxui::Event::ArrowLeft) {
            if (selected_col_ > 0) {
                selected_col_--;
                if (on_select_) on_select_(cells_[selected_row_][selected_col_]);
            }
            return true;
        }
        if (event == ftxui::Event::ArrowRight) {
            if (selected_col_ < GRID_SIZE - 1) {
                selected_col_++;
                if (on_select_) on_select_(cells_[selected_row_][selected_col_]);
            }
            return true;
        }
        if (event == ftxui::Event::Return) {
            if (selected_row_ >= 0 && selected_col_ >= 0) {
                if (on_select_) on_select_(cells_[selected_row_][selected_col_]);
            }
            return true;
        }
        return false;
    });
    
    // Handle mouse hover
    component |= ftxui::CatchEvent([this](ftxui::Event event) {
        if (event.is_mouse()) {
            auto mouse = event.mouse();
            // Simple hover detection - would need proper coordinate mapping in real implementation
            if (mouse.button == ftxui::Mouse::None && on_hover_) {
                // Approximate grid position from mouse
                // This is simplified - real implementation needs proper coordinate conversion
                int r = mouse.y / 2;  // Approximate
                int c = mouse.x / 5;  // Approximate
                if (r >= 0 && r < GRID_SIZE && c >= 0 && c < GRID_SIZE) {
                    hovered_row_ = r;
                    hovered_col_ = c;
                    on_hover_(cells_[r][c]);
                }
            }
        }
        return false;
    });
    
    return component;
}

const GridCell* HandGrid::getSelectedCell() const {
    if (selected_row_ >= 0 && selected_col_ >= 0) {
        return &cells_[selected_row_][selected_col_];
    }
    return nullptr;
}

void HandGrid::onSelect(std::function<void(const GridCell&)> callback) {
    on_select_ = callback;
}

void HandGrid::onHover(std::function<void(const GridCell&)> callback) {
    on_hover_ = callback;
}

void HandGrid::clearSelection() {
    selected_row_ = -1;
    selected_col_ = -1;
}

ftxui::Element HandGrid::RenderGrid() {
    ftxui::Elements rows;
    
    // Header row with rank labels - no spacing to save width
    ftxui::Elements header = {ftxui::text(" ") | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 2)};
    const char ranks[] = "AKQJT98765432";
    for (int c = 0; c < GRID_SIZE; ++c) {
        header.push_back(ftxui::text(std::string(1, ranks[c])) | ftxui::center | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 4));
    }
    rows.push_back(ftxui::hbox(header));
    
    // Grid rows
    for (int r = 0; r < GRID_SIZE; ++r) {
        ftxui::Elements row_elements;
        
        // Row label
        row_elements.push_back(ftxui::text(std::string(1, ranks[r])) | ftxui::center | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 2));
        
        // Cells - compact 4-char width, no spacing between cells
        for (int c = 0; c < GRID_SIZE; ++c) {
            row_elements.push_back(RenderCell(r, c) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 4));
        }
        
        rows.push_back(ftxui::hbox(row_elements));
    }
    
    return ftxui::vbox(rows) | ftxui::border;
}

ftxui::Element HandGrid::RenderCell(int row, int col) {
    const auto& cell = cells_[row][col];
    
    ftxui::Color bg = GetCellBackground(row, col);
    ftxui::Color fg = GetCellForeground(row, col);
    
    // Create cell content with proper sizing
    auto element = ftxui::text(cell.notation) | ftxui::color(fg);
    
    // Center the text within the cell
    element = element | ftxui::center;
    
    // Add selection highlight (inverted colors instead of border to avoid layout issues)
    if (row == selected_row_ && col == selected_col_) {
        element = element | ftxui::bold;
        // Use a lighter background for selection
        bg = ftxui::Color::RGB(100, 100, 100);
    }
    
    element = element | ftxui::bgcolor(bg);
    
    // Compact cell: 4 width x 1 height (fits 3-char notations like "AKs" tightly)
    return element | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 4) | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 1);
}

ftxui::Element HandGrid::RenderTooltip(int row, int col) {
    const auto& cell = cells_[row][col];
    const auto& strat = cell.strategy;
    
    auto dominant = strat.getDominantAction();
    
    ftxui::Elements lines = {
        ftxui::text(cell.notation) | ftxui::bold,
        ftxui::separator(),
        ftxui::text("Fold: " + std::to_string(static_cast<int>(strat.fold * 100)) + "%"),
        ftxui::text("Check: " + std::to_string(static_cast<int>(strat.check * 100)) + "%"),
        ftxui::text("Call: " + std::to_string(static_cast<int>(strat.call * 100)) + "%"),
        ftxui::text("Bet 20%: " + std::to_string(static_cast<int>(strat.bet_20 * 100)) + "%"),
        ftxui::text("Bet 33%: " + std::to_string(static_cast<int>(strat.bet_33 * 100)) + "%"),
        ftxui::text("Bet 52%: " + std::to_string(static_cast<int>(strat.bet_52 * 100)) + "%"),
        ftxui::text("Bet 100%: " + std::to_string(static_cast<int>(strat.bet_100 * 100)) + "%"),
        ftxui::text("Bet 123%: " + std::to_string(static_cast<int>(strat.bet_123 * 100)) + "%"),
        ftxui::text("Raise: " + std::to_string(static_cast<int>(strat.raise * 100)) + "%"),
        ftxui::separator(),
        ftxui::text("Dominant: " + dominant.first + " " + std::to_string(static_cast<int>(dominant.second * 100)) + "%")
    };
    
    return ftxui::vbox(lines) | ftxui::border;
}

ftxui::Color HandGrid::GetCellBackground(int row, int col) const {
    if (row == selected_row_ && col == selected_col_) {
        return COLOR_BG_HIGHLIGHT;
    }
    return cells_[row][col].strategy.getColor();
}

ftxui::Color HandGrid::GetCellForeground(int row, int col) const {
    // Use white text for contrast on colored backgrounds
    return COLOR_TEXT;
}

} // namespace ui
} // namespace poker
