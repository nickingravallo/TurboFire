#pragma once

#include <ftxui/screen/color.hpp>

namespace poker {
namespace ui {

// Action colors (not constexpr because ftxui::Color is not literal)
inline ftxui::Color COLOR_FOLD = ftxui::Color::Blue;
inline ftxui::Color COLOR_CALL = ftxui::Color::RGB(255, 107, 107); // Light red
inline ftxui::Color COLOR_BET = ftxui::Color::RGB(204, 0, 0);     // Deep red
inline ftxui::Color COLOR_CHECK = ftxui::Color::RGB(0, 170, 0);   // Green
inline ftxui::Color COLOR_RAISE = COLOR_BET;
inline ftxui::Color COLOR_ALL_IN = ftxui::Color::RGB(139, 0, 0);  // Dark red

// Background colors
inline ftxui::Color COLOR_BG_DARK = ftxui::Color::RGB(30, 30, 30);
inline ftxui::Color COLOR_BG_MID = ftxui::Color::RGB(45, 45, 45);
inline ftxui::Color COLOR_BG_LIGHT = ftxui::Color::RGB(60, 60, 60);

// Text colors
inline ftxui::Color COLOR_TEXT_WHITE = ftxui::Color::White;
inline ftxui::Color COLOR_TEXT_BLACK = ftxui::Color::Black;
inline ftxui::Color COLOR_TEXT_GRAY = ftxui::Color::GrayDark;

// Highlight colors
inline ftxui::Color COLOR_HIGHLIGHT = ftxui::Color::Yellow;
inline ftxui::Color COLOR_SELECTED = ftxui::Color::Cyan;

// Blend two colors by weight (0.0 = color1, 1.0 = color2)
ftxui::Color blendColors(ftxui::Color c1, ftxui::Color c2, float weight);

// Blend multiple action colors based on strategy
ftxui::Color blendStrategyColors(float fold, float check, float call, float bet, float raise);

} // namespace ui
} // namespace poker
