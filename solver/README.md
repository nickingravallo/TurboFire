# TurboFire solver

Build and run from the repository root with a C++17 compiler.

```sh
make -C solver
solver/turbofire 2s7sJsQs shared 3
make -C solver check
```

Enter three, four, or five consecutive cards in deal order, with ranks `23456789TJQKA` and suits `shdc`. Training begins at the street specified by the board, with a pot of 40 and stacks of 80. The defaults are `2s7sJsQs shared 1`. Each iteration traverses once per player. The reported hero EV is the first player's root counterfactual value for the last iteration, weighted and normalized over compatible pairs of private hands.

The first player is the BB, who three-bet preflop and acts first postflop. The second is the SB/button, who opened and called the three-bet. The executable reads `ranges/hu_bb_vs_sb/bb_3bet.csv` and `sb_call_3bet.csv` at startup from the repository range directory recorded by the Makefile. Edits take effect on the next run. These starter ranges each cover 20% of the 1,326 possible combinations before board blockers. For this example, the SB four-bets QQ+, AKs, AKo, A5s and A4s, so those classes have zero call weight.

Core callers can use `Hands::load_range(path, dead_cards)` for any range file. Use the CSV header `hand,weight` and one exact class per row, such as `JJ,1`, `KQs,0.5` or `AJo,0.25`. Write the higher rank first. Omitted classes have zero weight. Each legal physical combination inherits its class weight, and blocked combinations receive zero.

On every street, each action stores FP32 regret for every board-compatible private combo and FP32 cumulative strategy for the acting player’s positive-weight starting combos that are compatible with the board. Nodes on the same board reuse the hand index lists. The reported `strategy_bytes` counts both accumulator arrays. In `shared` mode a chance node visits one subtree per group of equivalent cards. The solver maps each physical card's private-hand values back into the parent's suit order before summing them. Both weighted ranges and the existing public board must admit the suit permutation. The `full` mode gives each physical card its own subtree. A flop start expands both undealt streets and can require many gigabytes.

## Configuring the betting tree

Pass a `BettingConfig` to the solver constructor. Each street has its own opening bet sizes, raise-to sizes, and cap, shared by both players. For example:

```cpp
BettingConfig betting{};
betting[FLOP].bet_pot_fractions = {0.33, 0.75, 1.0};
betting[FLOP].raise_to_multipliers = {2.5, 4.0};
betting[FLOP].raise_cap = 3;
Solver solver(flop, hero, villain, true, betting);
```

Here either player can open for 33%, 75%, or 100% of the current pot. Facing a bet of 10, a 2.5× raise targets a total street commitment of 25. The cap counts the opening bet, so 3 allows a bet, a raise, and a re-raise. Zero disables betting on that street. Defaults on every street are a half-pot bet, a 2.5× raise, and a cap of 2.

Sizes round down to whole chips and respect the minimum raise and effective stacks. Sizes that resolve to the same amount produce one branch. An empty sizing list disables that category. `solver.get_legal_actions(state)` returns the ordered branches and additional chips for each. Create a solver with the edited configuration to start training a new tree.

## Memory comparison

With the original example ranges and compact FP32, a 100-iteration shared solve on the flop `2s7sJs` uses 428,236,000 accumulator bytes across 42,454 action nodes. On the turn `2s7sJsQs`, shared mode uses 6,006,024 bytes across 578 action nodes and full mode uses 13,023,792 bytes across 1,254 nodes.

In the earlier dense FP64 implementation on Linux with GCC, `-O2 -std=c++17`, the four-spade board above, and three iterations, the median peak resident set across three fresh processes was:

| Mode | Action nodes | Regret and strategy bytes | Peak RSS (KiB) | Hero EV |
| --- | ---: | ---: | ---: | ---: |
| Full | 1,254 | 59,362,368 | 63,184 | 1.396267414 |
| Shared | 578 | 27,368,640 | 31,392 | 1.396267414 |

The 48 legal river cards form 22 groups on this board. Sharing reduced peak RSS by 50.3%. On `2s7hJdQc`, each card has its own group and both modes allocated 1,254 action nodes.

```sh
for mode in full shared; do
    for run in 1 2 3; do
        /usr/bin/time -f '%M KiB' solver/turbofire 2s7sJsQs "$mode" 3
    done
done
```

The original `cppsolver` executable used 4,096 KiB median peak RSS across three runs. Its training entry point deals a random flop, filters the ranges, and returns before allocating a betting tree. The full mode above measures memory for the same completed training workload with chance sharing disabled.
