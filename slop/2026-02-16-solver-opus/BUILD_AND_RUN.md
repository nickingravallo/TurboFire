# How to Build and Run the GTO Solver

## Quick Start

### 1. Build the Project

```bash
cd /home/nick/Projects/TurboFire/slop/2026-02-16-solver-opus
mkdir -p build
cd build
cmake ..
make
```

This will:
- Download ftxui library automatically (via FetchContent)
- Compile all source files
- Create the `GTOSolver` executable

### 2. Run the Solver

```bash
./GTOSolver
```

## Using the Interface

### Navigation
- **Arrow Keys** (↑↓←→): Move the selection cursor around the 13x13 hand grid
- **Space** or **Enter**: Solve for the currently selected hand
- **Q**: Quit the application

### Understanding the Display

1. **Top Panel**: Shows current street (Flop), pot size, and stack size
2. **Hand Grid**: 13x13 matrix showing all possible starting hands:
   - Rows: A, K, Q, J, T, 9, 8, 7, 6, 5, 4, 3, 2
   - Columns: A, K, Q, J, T, 9, 8, 7, 6, 5, 4, 3, 2
   - Pairs are on the diagonal (AA, KK, QQ, etc.)
   - Upper triangle: Suited hands (AKs, AQs, etc.)
   - Lower triangle: Offsuit hands (AKo, AQo, etc.)

3. **Color Coding** (after solving):
   - 🔵 **Blue**: Fold frequency (dominant action)
   - 🟢 **Green**: Check/Call frequency (dominant action)
   - 🔴 **Red**: Bet/Raise frequency (dominant action)
   - ⚫ **Dark Red**: All-in frequency (dominant action)
   - ⚪ **Gray**: Not yet solved

4. **Bottom Panel**: Shows:
   - Selected hand
   - Strategy breakdown with percentages for each action
   - Status messages

### Example Workflow

1. Start the application - you'll see the flop board (As Kh 2c by default)
2. Use arrow keys to navigate to a hand (e.g., AA in top-left)
3. Press Space or Enter to solve
4. The cell will turn a color based on the strategy
5. Check the bottom panel for detailed action frequencies
6. Navigate to other hands and solve them to build up your range view

### Changing the Flop

To use a different flop, edit `src/main.cpp`:

```cpp
// Change these cards (rank: 0-12 = 2-A, suit: 0-3 = spades, hearts, diamonds, clubs)
Card flop1(12, 0);  // As (Ace of spades)
Card flop2(11, 1);   // Kh (King of hearts)
Card flop3(0, 2);    // 2c (2 of clubs)
```

Then rebuild:
```bash
cd build
make
./GTOSolver
```

## Troubleshooting

### Build Errors

**CMake version too old:**
```bash
# Install CMake 3.20+ or use a package manager
sudo apt-get install cmake  # Ubuntu/Debian
```

**Compiler doesn't support C++20:**
```bash
# Use a newer compiler (GCC 10+, Clang 10+)
g++ --version  # Check version
```

**ftxui download fails:**
- Check internet connection
- CMake will try to download from GitHub
- If it fails, you can manually clone ftxui and point CMake to it

### Runtime Issues

**Terminal too small:**
- The TUI needs at least 80x24 characters
- Resize your terminal window

**Colors not showing:**
- Make sure your terminal supports 256 colors
- Try: `export TERM=xterm-256color`

## Current Limitations

⚠️ **Note**: The CFR solver is currently a placeholder. Strategies shown are based on simple heuristics (hand strength), not actual GTO calculations. The framework is ready for full CFR+ implementation.
