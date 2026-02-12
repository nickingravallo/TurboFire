# GTO Poker Solver - Implementation Plan

## Overview
A high-performance C++20 GTO (Game Theory Optimal) poker solver with a Terminal User Interface (TUI). Solves heads-up (SB vs BB) poker spots with full 1326 hand combinations using isomorphic abstraction for 10x+ speedup.

## Core Specifications

### General Settings
- **Language**: C++20
- **TUI Library**: ftxui
- **Build System**: CMake
- **Solver Algorithm**: MCCFR (Monte Carlo Counterfactual Regret Minimization)
- **Iterations**: 100,000 per solve (configurable)
- **Storage Format**: Binary
- **Storage Location**: `./solves/`

### Game Structure
- **Format**: Heads-up (SB vs BB)
- **Stack Size**: 100 big blinds (standard)
- **SB Open Size**: 2.5bb
- **Positions**: SB (OOP) vs BB (IP)

### Betting Options
- **Bet Sizes**: 20%, 33%, 52%, 100%, 123% of pot
- **Raise Size**: 2.5x
- **Actions**: Fold, Check, Call, Bet, Raise, All-in (explicit)

### Ranges
- **SB**: Standard GTO opening range (~40-50%)
- **BB**: Standard GTO defending range (~60-75% vs SB open)

## Architecture

### 1. Card & Hand System
```
Card: 0-51 representation (4 bits suit, 4 bits rank)
  - Suits: ♠ (0), ♥ (1), ♦ (2), ♣ (3)
  - Ranks: 2 (0), 3 (1), ..., T (8), J (9), Q (10), K (11), A (12)

Hand: 2-card combination
  - Raw cards
  - Precomputed properties (suited, pair, rank values)

Board: 3-5 card community cards
  - Flop: 3 cards
  - Turn: 4 cards  
  - River: 5 cards
```

### 2. Hand Evaluator
- **Method**: Perfect hash with lookup tables
- **Size**: ~200KB lookup table
- **Performance**: O(1) evaluation
- **Input**: 7 cards (2 hole + 5 board)
- **Output**: Hand rank (0-7462 for standard poker hands)

### 3. Isomorphic Abstraction
- **Raw Combinations**: 1,326 possible 2-card hands
- **Abstraction**: ~1,500 canonical buckets
- **Grouping Criteria**:
  - Rank pattern (e.g., AK, 72)
  - Suit relationship (suited, offsuit)
  - Board texture relationships (flush draws, backdoor draws)
- **Benefit**: 10x+ speedup with <1% EV loss

### 4. Game Tree
```
Node Structure:
├── Street (preflop/flop/turn/river)
├── Player to act (SB/BB)
├── Pot size
├── Current bet to call
├── Stack sizes (SB, BB)
├── Board cards
├── Action history
├── Available actions
├── Strategy (for solved nodes)
└── Child nodes
```

### 5. MCCFR Solver
- **Algorithm**: External sampling MCCFR
- **Iterations**: 100,000 (configurable)
- **Process**:
  1. Traverse game tree with external sampling
  2. Calculate counterfactual values
  3. Update regrets using regret matching
  4. Accumulate strategies
  5. Normalize to get final GTO strategy

### 6. Serialization System
- **Format**: Binary (fast, compact)
- **Location**: `./solves/`
- **Filename**: Hash of (board + action history)
- **Contents**:
  - Game tree node structure
  - Strategy probabilities
  - Regret values
  - Metadata (timestamp, iterations, EV)

## TUI Interface

### Layout
```
┌─────────────────────────────────────────────────────────────┐
│  Pot: $75    SB: $87.5    BB: $87.5    Street: FLOP         │
├─────────────────────────────────────────────────────────────┤
│  Board: [A♠] [K♥] [7♦]                                      │
├─────────────────────────────────────────────────────────────┤
│                    HAND GRID (AA-22)                        │
│     A   K   Q   J   T   9   8   7   6   5   4   3   2       │
│  A [AA][AKs][AQs][AJs][ATs][A9s][A8s][A7s][A6s][A5s][A4s]  │
│  K [AKo][KK][KQs][KJs][KTs][K9s][K8s][K7s][K6s][K5s][K4s]   │
│  Q [AQo][KQo][QQ][QJs][QTs][Q9s][Q8s][Q7s][Q6s][Q5s][Q4s]   │
│  ...                                                        │
├─────────────────────────────────────────────────────────────┤
│  History: BB checks → SB bets 33% → BB raises 2.5x → ?      │
├─────────────────────────────────────────────────────────────┤
│  [F]old  [C]heck  [L]Call  [B]et  [R]aise  [A]ll-in  [U]ndo │
└─────────────────────────────────────────────────────────────┘
```

### Color Scheme
```
Action      Color          Hex Code     Usage
─────────────────────────────────────────────────
Fold        Blue           #0066CC      Background blend
Call        Light Red      #FF6B6B      Background blend
Bet/Raise   Deep Red       #CC0000      Background blend  
Check       Green          #00AA00      Background blend
Text        White/Black    #FFFFFF      Contrasting text
```

### Grid Cell Visualization
Each cell (13×13 grid):
- **Background**: Blended color based on action frequencies
  - Pure blue = 100% fold
  - Pure green = 100% check
  - Pure light red = 100% call
  - Pure deep red = 100% bet/raise
  - Mixed = proportional blend of colors
- **Mini Pie Chart**: Overlay showing exact action distribution
- **Text**: Hand notation (e.g., "AKs", "72o", "AA")
- **Contrast**: White or black text for readability

### Progressive Input Flow
1. **Input Flop**: User enters 3 cards (e.g., "AsKh7d")
2. **OOP (SB) Action**: User selects check or bet
3. **IP (BB) Response**: User selects fold/call/raise
4. **Input Turn**: User enters 4th card
5. **Continue Actions**: OOP → IP actions
6. **Input River**: User enters 5th card
7. **Final Actions**: To showdown
8. **Solve**: Run MCCFR on current node

### Controls
```
Key    Action
─────────────────
F      Fold
C      Check
L      Call
B      Bet (select size)
R      Raise (2.5x)
A      All-in
U      Undo last action
Enter  Confirm/Proceed
Q      Quit
```

## File Structure
```
poker_solver/
├── CMakeLists.txt
├── config.json              # Solver configuration
├── src/
│   ├── main.cpp
│   ├── card/
│   │   ├── card.h/.cpp      # Card representation
│   │   ├── hand.h/.cpp      # Hand representation
│   │   └── evaluator.h/.cpp # 7-card hand evaluator
│   ├── game/
│   │   ├── range.h/.cpp     # Range definitions
│   │   ├── board.h/.cpp     # Board state
│   │   ├── game_tree.h/.cpp # Game tree structure
│   │   └── action.h/.cpp    # Action definitions
│   ├── solver/
│   │   ├── cfr.h/.cpp       # MCCFR implementation
│   │   ├── abstraction.h/.cpp # Isomorphic abstraction
│   │   └── solver.h/.cpp    # High-level solver interface
│   ├── storage/
│   │   └── serialize.h/.cpp # Binary serialization
│   └── tui/
│       ├── app.h/.cpp       # Main TUI application
│       ├── grid.h/.cpp      # Hand grid component
│       ├── colors.h         # Color definitions
│       └── components.h/.cpp # Reusable TUI components
├── include/
│   └── poker_solver/        # Public headers
└── solves/                  # Solved spots storage
```

## Implementation Phases

### Phase 1: Core Infrastructure
- [ ] Card representation system
- [ ] Hand evaluator with lookup tables
- [ ] Hand isomorphism system
- [ ] Range definitions (SB/BB)

### Phase 2: Game Logic  
- [ ] Board state management
- [ ] Game tree construction
- [ ] Action validation
- [ ] Pot size calculations

### Phase 3: Solver Engine
- [ ] MCCFR implementation
- [ ] Strategy computation
- [ ] External sampling
- [ ] Convergence checking

### Phase 4: Storage
- [ ] Binary serialization
- [ ] File I/O operations
- [ ] Hash-based indexing
- [ ] Cache management

### Phase 5: TUI
- [ ] Main application layout
- [ ] Hand grid component with colors
- [ ] Pie chart rendering
- [ ] Input handling
- [ ] Progress indicators

### Phase 6: Integration
- [ ] Progressive input flow
- [ ] Action history tracking
- [ ] Undo/redo functionality
- [ ] Solve trigger integration

### Phase 7: Polish
- [ ] Performance optimization
- [ ] Error handling
- [ ] Edge case testing
- [ ] Documentation

## Key Algorithms

### Hand Isomorphism
```cpp
// Group hands by canonical representation
uint32_t get_canonical_id(Hand hand, Board board) {
    // 1. Get rank pattern (e.g., AK, 72)
    // 2. Determine suit relationship type
    // 3. Map to canonical bucket ID
}
```

### MCCFR Regret Matching
```cpp
// Update strategy based on regrets
void regret_matching(Node* node) {
    for (Action action : node->actions) {
        node->strategy[action] = max(0, node->regret[action]);
    }
    normalize(node->strategy);
}
```

### Color Blending
```cpp
// Blend colors based on action frequencies
Color blend_actions(Strategy strat) {
    Color result = {0, 0, 0};
    result += strat.fold * BLUE;
    result += strat.check * GREEN;
    result += strat.call * LIGHT_RED;
    result += (strat.bet + strat.raise) * DEEP_RED;
    return result;
}
```

## Performance Targets

- **Hand Evaluation**: < 1μs per hand
- **Solver Iteration**: 100k iterations in 30-60 seconds
- **UI Response**: < 16ms (60 FPS)
- **Memory Usage**: < 500MB for full game tree
- **Storage**: < 10MB per solved spot

## Future Enhancements

- Multi-threaded MCCFR
- GPU acceleration (CUDA)
- More bet sizing options
- Different game formats (3-handed, tournaments)
- EV calculations and exports
- Hand history imports

---

**Status**: Ready for implementation  
**Last Updated**: 2026-02-12
