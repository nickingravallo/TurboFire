# MCCFR Poker Solver

A Monte Carlo Counterfactual Regret Minimization (MCCFR) implementation for learning Nash Equilibrium strategies in Heads-Up No-Limit Texas Hold'em poker.

## Overview

This solver uses **External Sampling MCCFR** to compute game-theoretic optimal (GTO) strategies for heads-up poker. It's designed to be educational, with extensive comments explaining the algorithm at every step.

### Key Features

- **External Sampling MCCFR**: Efficient variant that samples opponent actions
- **Fast Hand Evaluation**: Two Plus Two algorithm with 125MB lookup table
- **Sparse Strategy Storage**: Hash table with ~1M entry capacity
- **Full Betting Structure**: 100bb deep, 4 streets (preflop→flop→turn→river)
- **Multiple Bet Sizes**: 20%, 33%, 52%, 100% pot + 2.5x raises
- **Progress Tracking**: Real-time progress bar and statistics
- **Checkpointing**: Save/resume training every 100k iterations

## Algorithm Explanation

### What is MCCFR?

**Counterfactual Regret Minimization (CFR)** is an iterative algorithm that finds Nash equilibrium in two-player zero-sum games through self-play.

**Monte Carlo CFR (MCCFR)** improves efficiency by:
1. Sampling random game states instead of exploring all
2. Using external sampling for opponent actions
3. Converging faster with less memory

### Core Concepts

**Information Sets**: States that look the same to a player (their cards + public actions)

**Regret**: For each action, how much better would it have been vs. current strategy?
```
Regret[action] = Value[action] - Value[current_strategy]
```

**Regret Matching**: Convert regrets to strategy
```
Strategy[action] = max(0, Regret[action]) / sum(max(0, Regret[all]))
```

**External Sampling**: When traversing the game tree:
- At opponent nodes: Sample ONE action
- At player nodes: Evaluate ALL actions
- Update regrets based on counterfactual values

## Building

### Prerequisites

- GCC or compatible C compiler
- ~150MB disk space (for lookup table)
- 4GB+ RAM recommended

### Quick Start

```bash
# Clone and enter directory
cd poker_solver

# Build everything
make all

# Generate hand evaluation table (one-time, ~5-10 minutes)
make table

# Run training (1M iterations)
./bin/poker_solver
```

### Build Options

```bash
make all       # Build solver and table generator
make table     # Generate lookup table
make run       # Build and run
make debug     # Debug build with symbols
make clean     # Clean build artifacts
make cleanall  # Clean everything including data
```

## Usage

### Training from Scratch

```bash
./bin/poker_solver
```

Training will:
1. Load hand evaluation table
2. Run 1M iterations with progress bar
3. Save checkpoint every 100k iterations
4. Export final strategy to `data/final_strategy.dat`

### Resume from Checkpoint

```bash
./bin/poker_solver resume data/checkpoint_100000.dat
```

### Custom Training Parameters

```bash
# Train for 500k iterations with checkpoints every 50k
./bin/poker_solver -i 500000 -c 50000
```

## Game Structure

### Betting Options

**When no bet to call:**
- Check
- Bet 20% pot
- Bet 33% pot  
- Bet 52% pot
- Bet 100% pot

**When facing a bet:**
- Fold
- Call
- Raise 2.5x

### Starting Conditions

- **Stacks**: 100bb each (50bb effective)
- **Blinds**: IP posts 0.5bb, OOP posts 1bb
- **First to Act**: IP preflop, OOP postflop

## Output

During training you'll see:
```
[================================================> ] 100.00% | Iter: 1000000/1000000 | 1250.5 iter/s | ETA: 0s
```

Final output includes:
- Total training time
- Iterations per second
- Strategy table statistics
- Memory usage

## File Structure

```
poker_solver/
├── include/           # Header files
│   ├── cards.h       # Card representation
│   ├── evaluator.h   # Hand evaluation
│   ├── actions.h     # Betting actions
│   ├── game_state.h  # Game tree
│   ├── strategy.h    # Regret/strategy storage
│   └── mccfr.h       # Core MCCFR algorithm
├── src/              # Source files
│   ├── main.c        # Entry point
│   ├── cards.c       # Card utilities
│   ├── evaluator.c   # Hand evaluator
│   ├── actions.c     # Action logic
│   ├── game_state.c  # State transitions
│   ├── strategy.c    # Hash table storage
│   ├── mccfr.c       # CFR traversal
│   └── generate_table.c  # Table generator
├── data/             # Data files
│   ├── hand_ranks.dat    # ~125MB lookup table
│   ├── checkpoint_*.dat  # Training checkpoints
│   └── final_strategy.dat
├── Makefile
└── README.md
```

## Implementation Details

### Card Representation

Cards are integers 0-51:
- Bits: `suit * 13 + rank`
- Ranks: 0=2, 1=3, ..., 11=K, 12=A
- Suits: 0=♣, 1=♦, 2=♥, 3=♠

### Hand Evaluation

Uses the **Two Plus Two** algorithm:
- Pre-computed 125MB lookup table
- Perfect hash for 5-7 card hands
- 0 = Royal Flush (best), 7462 = worst

### Strategy Storage

Sparse hash table with linear probing:
- Initial capacity: 1M entries
- Grows automatically at 75% load
- Stores regrets + strategy sums per info set

### Info Set Key

```
Key = Hash(cards | street | action_history)
```

Example: `"AsKs|flop|b20c"` = A♠K♠ on flop after 20% bet and call

## Algorithm Pseudocode

```python
# MCCFR Training Loop
for iteration in range(N):
    # Sample a random hand
    cards = deal_random_hand()
    
    # Train both players on this hand
    for player in [IP, OOP]:
        traverse(game_root, player)

def traverse(state, traversing_player):
    if state.is_terminal:
        return state.payoff
    
    if state.current_player == traversing_player:
        # Player node: explore ALL actions
        strategy = regret_matching(state.info_set)
        
        action_values = []
        for action in legal_actions:
            value = traverse(state.apply(action), traversing_player)
            action_values.append(value)
        
        # Update regrets
        node_value = sum(strategy[i] * action_values[i])
        for i in range(len(actions)):
            regret = action_values[i] - node_value
            regrets[action] += regret
        
        return node_value
    else:
        # Opponent node: sample ONE action (external sampling)
        action = sample_opponent_action(state.info_set)
        return traverse(state.apply(action), traversing_player)
```

## Learning Resources

### Papers
- **Zinkevich et al. (2007)**: "Regret Minimization in Games with Incomplete Information" (Original CFR)
- **Lanctot et al. (2009)**: "Monte Carlo Sampling for Regret Minimization in Extensive Games" (MCCFR)
- **Neller & Lanctot (2013)**: "An Introduction to Counterfactual Regret Minimization"

### Key Concepts
1. **Nash Equilibrium**: No player can improve by changing strategy unilaterally
2. **Regret**: Opportunity cost of not playing the best action
3. **Counterfactual**: "What if I had played differently?"
4. **Information Set**: Game states indistinguishable to a player

## Performance

Typical performance on modern hardware:
- **~1000-2000 iterations/second**
- **1M iterations in ~10-15 minutes**
- **Convergence**: 1M iterations sufficient for learning
- **Memory**: ~200-500MB for strategy tables

## Limitations

1. **Abstraction**: Simplified betting structure (no all-in, limited bet sizes)
2. **Two Player**: Only heads-up (not multi-way)
3. **No Card Removal**: Doesn't account for known cards perfectly
4. **Storage**: Strategy tables grow large with training

## Future Improvements

- [ ] Add all-in action
- [ ] Implement CFR+ (faster convergence)
- [ ] Add linear CFR (decreasing regret weights)
- [ ] Support multi-way pots
- [ ] GPU acceleration for larger games
- [ ] Real-time strategy lookup during play

## License

MIT License - Educational use encouraged!

## Credits

Built for learning MCCFR and game theory. Inspired by:
- The original CFR paper by Zinkevich et al.
- DeepStack and Libratus poker AIs
- Two Plus Two poker forum hand evaluation

## Troubleshooting

### "Cannot load hand evaluation table"
Run `make table` to generate the lookup table.

### Out of memory
Reduce hash table capacity in `strategy.h`:
```c
#define INITIAL_TABLE_CAPACITY (1 << 20)  // Try 1<<18 or 1<<16
```

### Slow training
- Use release build: `make clean && make all`
- Disable pruning: Set `.use_pruning = false` in config
- Reduce checkpoint frequency

### Want to visualize strategies
Export and use external tools:
```bash
./bin/poker_solver
# Then process data/final_strategy.dat
```

---

Happy solving! 🃏
