#pragma once

#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <vector>
#include <string>
#include "../core/ranges.hpp"

namespace gto {

// GTO Wizard-style color palette
namespace colors {
    const ftxui::Color fold = ftxui::Color::RGB(41, 121, 255);     // Blue
    const ftxui::Color call = ftxui::Color::RGB(0, 200, 83);       // Green
    const ftxui::Color bet = ftxui::Color::RGB(255, 23, 68);       // Red
    const ftxui::Color shove = ftxui::Color::RGB(213, 0, 0);       // Dark Red
    const ftxui::Color background = ftxui::Color::RGB(30, 30, 30);
    const ftxui::Color grid_bg = ftxui::Color::RGB(45, 45, 45);
}

// Render a single hand cell with frequency bars
ftxui::Element RenderHandCell(
    const std::string& hand_name,
    float fold_freq,
    float call_freq,
    float bet_freq,
    float shove_freq = 0.0f
) {
    using namespace ftxui;
    
    // Normalize frequencies
    float total = fold_freq + call_freq + bet_freq + shove_freq;
    if (total > 0.0f) {
        fold_freq /= total;
        call_freq /= total;
        bet_freq /= total;
        shove_freq /= total;
    }
    
    // Hand label
    auto label = text(hand_name) | center | color(Color::White) | bold;
    
    // Build frequency bar using flexbox
    Elements bars;
    
    if (fold_freq > 0.01f) {
        bars.push_back(
            text("") | bgcolor(colors::fold) | flex
        );
    }
    if (call_freq > 0.01f) {
        bars.push_back(
            text("") | bgcolor(colors::call) | flex
        );
    }
    if (bet_freq > 0.01f) {
        bars.push_back(
            text("") | bgcolor(colors::bet) | flex
        );
    }
    if (shove_freq > 0.01f) {
        bars.push_back(
            text("") | bgcolor(colors::shove) | flex
        );
    }
    
    if (bars.empty()) {
        bars.push_back(text("") | flex);
    }
    
    auto frequency_bar = hbox(std::move(bars)) | size(HEIGHT, EQUAL, 1);
    
    return vbox({label, frequency_bar}) 
        | border 
        | size(WIDTH, EQUAL, 6) 
        | size(HEIGHT, EQUAL, 3)
        | bgcolor(colors::grid_bg);
}

// Render the 13x13 hand matrix
ftxui::Element RenderRangeGrid(
    const std::vector<float>& fold_freqs,
    const std::vector<float>& call_freqs,
    const std::vector<float>& bet_freqs,
    const std::vector<float>& shove_freqs = {}
) {
    using namespace ftxui;
    
    Elements rows;
    
    // Build 13x13 grid
    constexpr std::array<const char*, 13> rank_chars = {
        "A", "K", "Q", "J", "T", "9", "8", "7", "6", "5", "4", "3", "2"
    };
    
    for (int r = 0; r < 13; ++r) {
        Elements row_cells;
        
        for (int c = 0; c < 13; ++c) {
            std::string hand_name;
            int idx;
            
            if (r == c) {
                // Pocket pair
                hand_name = std::string(rank_chars[r]) + rank_chars[c];
                idx = r * 13 + c;
            } else if (r < c) {
                // Suited (row < col means higher card first)
                hand_name = std::string(rank_chars[r]) + rank_chars[c] + "s";
                idx = r * 13 + c;
            } else {
                // Offsuit
                hand_name = std::string(rank_chars[c]) + rank_chars[r] + "o";
                idx = c * 13 + r;
            }
            
            float fold = (idx < static_cast<int>(fold_freqs.size())) ? fold_freqs[idx] : 0.0f;
            float call = (idx < static_cast<int>(call_freqs.size())) ? call_freqs[idx] : 0.0f;
            float bet = (idx < static_cast<int>(bet_freqs.size())) ? bet_freqs[idx] : 0.0f;
            float shove = (!shove_freqs.empty() && idx < static_cast<int>(shove_freqs.size())) 
                ? shove_freqs[idx] : 0.0f;
            
            row_cells.push_back(RenderHandCell(hand_name, fold, call, bet, shove));
        }
        
        rows.push_back(hbox(std::move(row_cells)));
    }
    
    return vbox(std::move(rows)) | bgcolor(colors::background);
}

// Render a legend for the colors
ftxui::Element RenderLegend() {
    using namespace ftxui;
    
    auto legend_item = [](const std::string& label, ftxui::Color bg_color) {
        Elements items;
        items.push_back(text("  ") | bgcolor(bg_color));
        items.push_back(text(" " + label) | ftxui::color(Color::White));
        return hbox(std::move(items));
    };
    
    Elements legend_items;
    legend_items.push_back(legend_item("Fold", colors::fold));
    legend_items.push_back(text("  "));
    legend_items.push_back(legend_item("Call", colors::call));
    legend_items.push_back(text("  "));
    legend_items.push_back(legend_item("Bet", colors::bet));
    legend_items.push_back(text("  "));
    legend_items.push_back(legend_item("Shove", colors::shove));
    
    return hbox(std::move(legend_items)) | border | color(Color::White);
}

// Main range viewer component
class RangeViewer {
public:
    struct DisplayData {
        std::string title;
        std::vector<float> fold_freqs;
        std::vector<float> call_freqs;
        std::vector<float> bet_freqs;
        std::vector<float> shove_freqs;
    };
    
    explicit RangeViewer(const DisplayData& data) : data_(data) {}
    
    ftxui::Element Render() const {
        using namespace ftxui;
        
        auto title = text(data_.title) | bold | center | color(Color::White);
        auto grid = RenderRangeGrid(data_.fold_freqs, data_.call_freqs, data_.bet_freqs, data_.shove_freqs);
        auto legend = RenderLegend();
        
        Elements items;
        items.push_back(title);
        items.push_back(separator());
        items.push_back(grid);
        items.push_back(separator());
        items.push_back(legend);
        
        return vbox(std::move(items)) 
            | border 
            | bgcolor(colors::background);
    }
    
    void UpdateData(const DisplayData& data) {
        data_ = data;
    }

private:
    DisplayData data_;
};

// Convert strategy to frequencies for display
// strategy: [action0_hand0, action0_hand1, ..., action1_hand0, action1_hand1, ...]
std::vector<std::vector<float>> StrategyToFrequencies(
    const std::vector<float>& strategy,
    size_t num_actions,
    size_t num_hands = 169
) {
    std::vector<std::vector<float>> freqs(num_actions, std::vector<float>(num_hands, 0.0f));
    
    for (size_t a = 0; a < num_actions; ++a) {
        for (size_t h = 0; h < num_hands; ++h) {
            freqs[a][h] = strategy[a * num_hands + h];
        }
    }
    
    return freqs;
}

} // namespace gto
