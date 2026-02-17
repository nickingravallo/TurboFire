# TurboFire

A work-in-progress GTO Solver

## Flop solver TUI

A terminal UI for viewing GTO strategy on a 13×13 hand grid (like Rangefinder). Load two ranges (OOP and IP), set a flop board, solve with MCCFR, then move the cursor over hands to see the action breakdown (Fold / Call / Raise %).

### Build (requires ncurses)

```bash
make flop_tui
```

Requires `libncurses-dev` (Debian/Ubuntu) or `ncurses-devel` (Fedora).

### Run

```bash
./output/flop_tui <oop_range.json> <ip_range.json> <board>
```

Example (using ranges in `data/ranges/`):

```bash
./output/flop_tui data/ranges/oop.json data/ranges/ip.json AhKhQd
```

Range JSON format matches [Rangefinder](rangefinder/README.md): `hands` object with keys like `AA`, `AKs`, `AKo` and values 0–1.

### TUI keys

| Action | Keys |
|--------|------|
| **Move cursor** | **W** / **A** / **S** / **D** or arrow keys |
| **Solve** | **S** (run MCCFR; may take a moment) |
| **Street** | **f** Flop, **t** Turn, **r** River (display; solver is flop-only) |
| **Quit** | **q** or **Escape** |

The status line shows the current hand and its strategy (Fold %, Call %, Raise %) when solved.

## How is AI used?

AI is used in generating prototypes (failed prototypes in the 'slop' directory) and the learning process for the actual solver. All code for the rest of the project is manually reviewed and rewritten, or written from scratch.
