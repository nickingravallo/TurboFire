# TurboFire solver

Build and run from the repository root with a C++17 compiler.

```sh
make -C solver
solver/turbofire 2s7sJsQs shared 3
make -C solver check
```

Enter three, four, or five consecutive cards in deal order, with ranks `23456789TJQKA` and suits `shdc`. Training begins at the street specified by the board, with a pot of 40 and stacks of 80. The defaults are `2s7sJsQs shared 1`. Each iteration traverses once per player. The reported hero EV is the first player's root counterfactual value for the last iteration, weighted and normalized over compatible pairs of private hands.

Both modes use the betting rules and starting ranges copied from `cppsolver`. On every street, each action stores FP32 regret for every board-compatible private combo and FP32 cumulative strategy for the acting player’s positive-weight starting combos that are compatible with the board. Nodes on the same board reuse the hand index lists. The reported `strategy_bytes` counts both accumulator arrays. In `shared` mode a chance node visits one subtree per group of equivalent cards. The solver maps each physical card's private-hand values back into the parent's suit order before summing them. Both weighted ranges and the existing public board must admit the suit permutation. The `full` mode gives each physical card its own subtree. A flop start expands both undealt streets and can require many gigabytes.

## Memory comparison

With compact FP32, a 100-iteration shared solve on the flop `2s7sJs` uses 428,236,000 accumulator bytes across 42,454 action nodes. On the turn `2s7sJsQs`, shared mode uses 6,006,024 bytes across 578 action nodes and full mode uses 13,023,792 bytes across 1,254 nodes.

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
