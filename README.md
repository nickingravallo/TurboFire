# TurboFire

A work-in-progress GTO Solver

## TurboFire solver TUI

A terminal UI for viewing GTO strategy on a 13×13 hand grid (like Rangefinder). Load two ranges (OOP and IP), set a 3-5 card board string, solve with MCCFR, then move the cursor over hands to see the action breakdown (Fold / Call / Raise %). When the cursor is on a suited cell, the status line shows all four suit combinations (e.g. **AKs** -> AsKs AhKh AdKd AcKc).

### Build (requires ncurses)

```bash
make
```

Requires `libncurses-dev` (Debian/Ubuntu) or `ncurses-devel` (Fedora).

### Run

```bash
./output/turbofire <oop_range.json> <ip_range.json> <board>
```

Example (using ranges in `data/ranges/`):

```bash
./output/turbofire data/ranges/oop.json data/ranges/ip.json AhKhQd3c4d
```

Range JSON format matches [Rangefinder](rangefinder/README.md): `hands` object with keys like `AA`, `AKs`, `AKo` and values 0–1.

### TUI keys

| Action | Keys |
|--------|------|
| **Move cursor** | **W** / **A** / **S** / **D** or arrow keys |
| **Solve** | **S** (run MCCFR; may take a moment) |
| **Street** | Solving progresses flop -> turn -> river in-tree; optional turn/river can be pre-specified in the input board string |
| **Quit** | **q** or **Escape** |

The status line shows the current hand and its strategy (Fold %, Call %, Raise %) when solved. On suited hands it also shows all four suit combos (e.g. AsKs AhKh AdKd AcKc).

## How is AI used?

AI is used in generating prototypes (failed prototypes in the 'slop' directory) and the learning process for the actual solver. All code for the rest of the project is manually reviewed and rewritten, or written from scratch.
