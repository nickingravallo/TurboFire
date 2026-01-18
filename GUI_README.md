# TurboFire GUI

## Overview
The GUI module provides a graphical interface to visualize GTO strategies with color-coded action frequencies.

## Features
- **Color-coded strategy display:**
  - Blue: Check/Call percentage
  - Green: Bet/Raise percentage  
  - Red: Fold percentage
- **Interactive range grid:** 13x13 grid showing all poker hand combinations
- **Hover tooltips:** Show exact percentages when hovering over cells
- **Street navigation:** Buttons to switch between Flop, Turn, and River
- **Board display:** Shows the board cards for each street

## Building with GUI Support

### Install SDL2 dependencies:

**Ubuntu/Debian:**
```bash
sudo apt-get install libsdl2-dev libsdl2-ttf-dev
```

**macOS:**
```bash
brew install sdl2 sdl2_ttf
```

### Build:
```bash
make gto-solver-gui
```

## Usage

Launch with GUI:
```bash
./output/TurboFire "22+,A2s+" "22+,A2s+" --gui
```

The GUI will launch after the terminal analysis completes. You can:
- Click the Flop/Turn/River buttons to switch streets
- Hover over cells in the grid to see exact percentages
- Close the window to exit

## Notes

- The GUI complements the terminal output - both will be shown
- Random boards are generated automatically if not specified
- The GUI shows aggregated strategy data from all hand combinations tested
