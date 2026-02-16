# GTO Poker Solver

A terminal-based poker solver with interactive TUI for exploring GTO strategies across all streets (preflop, flop, turn, river).

## Features

- **Multi-Street FSM**: Complete state machine handling preflop, flop, turn, and river
- **Interactive TUI**: Clickable 13x13 hand grid (AA-22) with color-coded strategies
- **On-Demand Solving**: Strategies computed only when you interact with cells (no presolving)
- **GTO Wizard-style Colors**:
  - Blue: Fold frequency
  - Green: Check/Call frequency
  - Red: Bet/Raise frequency
  - Dark Red: All-in frequency
- **Standard GTO Ranges**: SB vs BB heads-up ranges (~96% SB opening)

## Building

### Requirements

- CMake 3.20+
- C++20 compatible compiler
- Git (for fetching ftxui)

### Build Steps

```bash
mkdir build
cd build
cmake ..
make
```

The executable will be `GTOSolver` in the build directory.

## Usage

Run the solver:
```bash
./GTOSolver
```

### Controls

- **Arrow Keys**: Navigate the hand grid
- **Space/Enter**: Solve for selected hand
- **Q**: Quit (standard ftxui quit)

### How It Works

1. The solver starts with a SB vs BB heads-up spot
2. Navigate to any hand in the 13x13 grid
3. Press Space/Enter to trigger on-demand CFR solving
4. The cell will be color-coded based on the dominant action frequency
5. View detailed strategy breakdown in the info panel

## Project Structure

```
gto-solver/
├── CMakeLists.txt
├── include/
│   ├── core/
│   │   ├── action.hpp          # Action types (Fold, Check, Call, Bet, Raise, All-in)
│   │   ├── bet_sizing.hpp      # Bet sizing abstraction
│   │   ├── card.hpp            # Card representation
│   │   ├── game_state.hpp      # Multi-street FSM
│   │   ├── hand.hpp            # Hand representation (AA, AKs, etc.)
│   │   ├── hand_evaluator.hpp  # 5-card and 7-card evaluation
│   │   ├── ranges.hpp          # Standard GTO ranges
│   │   └── strategy.hpp        # Strategy/action frequencies
│   ├── solver/
│   │   └── cfr_engine.hpp      # CFR+ solver engine
│   └── ui/
│       └── interactive_range_display.hpp  # Interactive TUI
└── src/
    ├── main.cpp
    ├── core/
    ├── solver/
    └── ui/
```

## Implementation Status

### Completed
- ✅ Core FSM with all streets
- ✅ Action validation (raise caps, minimum raises, etc.)
- ✅ Street transitions
- ✅ Interactive TUI with color coding
- ✅ On-demand solving framework
- ✅ Standard GTO ranges

### TODO
- ⏳ Full CFR+ implementation (currently placeholder)
- ⏳ Tree building for game tree
- ⏳ Complete strategy extraction from CFR solution
- ⏳ Board card selection UI
- ⏳ Action sequence builder

## License

MIT License
