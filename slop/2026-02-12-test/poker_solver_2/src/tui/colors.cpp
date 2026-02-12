#include "tui/colors.h"

namespace poker {
namespace ui {

ftxui::Color blendColors(ftxui::Color c1, ftxui::Color c2, float weight) {
    // Simplified blending - just return the dominant color
    if (weight > 0.5f) return c2;
    return c1;
}

ftxui::Color blendStrategyColors(float fold, float check, float call, float bet, float raise) {
    // Normalize
    float total = fold + check + call + bet + raise;
    if (total == 0.0f) return COLOR_BG_MID;
    
    fold /= total;
    check /= total;
    call /= total;
    bet /= total;
    raise /= total;
    
    // Start with background
    ftxui::Color result = COLOR_BG_DARK;
    
    // Blend in each action color
    result = blendColors(result, COLOR_FOLD, fold * 0.8f);
    result = blendColors(result, COLOR_CHECK, check * 0.8f);
    result = blendColors(result, COLOR_CALL, call * 0.8f);
    result = blendColors(result, COLOR_BET, bet * 0.8f);
    result = blendColors(result, COLOR_RAISE, raise * 0.8f);
    
    return result;
}

} // namespace ui
} // namespace poker
