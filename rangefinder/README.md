# Rangefinder

A terminal UI (TUI) for building poker ranges on a 13×13 hand grid. Set a percentage (0–100%) per hand for “what the actor will have,” save ranges as JSON in `data/ranges/` (including subfolders). Output is intended for use with a poker solver.

## Run

```bash
cd rangefinder
pip install -e .
python -m rangefinder
```

Or after install: `rangefinder`

---

## Usage

### Main screen (grid)

The status line shows the **current file** (e.g. `Editing: btn_open_50.json`) or `New range` if none is open. Movement uses **WASD by default**; you can cycle layouts with **m** (hjkl → jikl → wasd).

| Action | Keys |
|--------|------|
| **Move cursor** | **W** / **A** / **S** / **D** (default), or **h**/**j**/**k**/**l**, or **j**/**i**/**k**/**l**, or **arrow keys** |
| **Extend selection** (rectangle) | **Shift** + movement key or **Shift+arrows** |
| **Clear selection** | **Escape** |
| **Set weight 0–90%** | **0**–**9** (applies to selection or current cell) |
| **Set weight 100%** | **f** or **F** |
| **Save / modify range** | **v** → opens save screen (see below) |
| **Load range** | **o** → browse `data/ranges/`, Enter to open folder or load file |
| **Clear grid** | **c** (also clears "current file") |
| **Cycle key layout** | **m** (hjkl → jikl → wasd) |
| **Quit** | **q** or **Ctrl+q** |

Colors on the grid scale with weight: **1** (10%) is lightest red, **5** (50%) deeper, **10**/F (100%) deepest. Each step 1–10 is distinct.

### Save screen (**v**)

- **s** — **Save current file**: overwrite the file you have open (only shown when a file is open). Closes the save screen.
- **f** — **Save as**: focus filename field; type a name and Enter to save to the current folder (or a new file path). Becomes the new "current file."
- **n** — **New folder**: focus "new folder" input; type a name and Enter to create a folder, then use **f** to save with a new filename there.
- **j** / **k** or **↑** / **↓** — Move in the list.
- **Enter** — Open a folder or activate the highlighted option (e.g. "(new folder)", "Save as (f)").
- **Escape** / **q** — Back to grid (cancel).

Ranges are stored under `data/ranges/`; you can use subfolders. Saving with **f** (and optionally **n**) writes to the current folder or a new one.

### Load screen (**o**)

- **j** / **k** or **↑** / **↓** — Move in the list.
- **Enter** — Open a folder or load the selected `.json` file (sets it as the current file).
- **Shift+j** or **Shift+k** on a file — Start "move file": navigate to the destination folder, then **Enter** to move the file there.
- **Escape** — Cancel move mode or go back one level; **q** — Back to grid.

You cannot create new folders from the Load screen; only browse and load (or move) existing files.

---

## Example JSON (data/ranges/)

Ranges are saved under `data/ranges/<name>.json` (or `data/ranges/<folder>/<name>.json` for subfolders) in this format:

```json
{
  "name": "btn_open_50",
  "description": "BTN open 50bb",
  "hands": {
    "AA": 1.0,
    "KK": 1.0,
    "AKs": 1.0,
    "AKo": 0.8,
    "QQ": 1.0,
    "QJs": 1.0,
    "JJ": 1.0,
    "TT": 1.0
  }
}
```

- **hands**: Object from hand string to weight in **[0, 1]** (0 = never, 1 = always). Standard preflop notation: `AA`, `AKs`, `AKo`, etc. (ranks `23456789TJQKA`).
- **name** / **description**: Optional; for your own bookkeeping; a solver can ignore them.

Only hands with weight &gt; 0 need to be present; the solver can treat missing hands as 0.

## Utility for a poker solver

A C++ (or other) solver can:

1. Read a range JSON from `data/ranges/<name>.json`.
2. Map each key in `hands` to its internal hand type (e.g. `Hand::from_string("AKs")`).
3. Use the value as a weight in [0, 1] for that hand.

The 13×13 grid mapping matches a common layout: pairs on the diagonal, suited above, offsuit below; rows/columns A→2. Hand strings (`AA`, `AKs`, `AKo`, …) are standard and can be parsed once per hand type.
