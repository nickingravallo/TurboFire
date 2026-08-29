#ifndef CONFIG_H
#define CONFIG_H

// Highest street to solve (0=flop, 1=turn, 2=river). Betting on this street
// is included; no cards are dealt beyond it.
//
// Makefile overrides these:
//   make            → turbofire       MAX_STREET=0 WALK_TREE=1  (flop soft labels)
//   make turn       → turbofire_turn  MAX_STREET=1 WALK_TREE=0  (flop+turn solve)
//   make river      → turbofire_river MAX_STREET=2 WALK_TREE=1  (through-river labels)
#ifndef MAX_STREET
#define MAX_STREET 0
#endif

#ifndef WALK_TREE
#define WALK_TREE 1
#endif

#endif // CONFIG_H
