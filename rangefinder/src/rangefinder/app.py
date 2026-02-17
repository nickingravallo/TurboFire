"""Rangefinder TUI: main app, grid screen."""

from __future__ import annotations

from pathlib import Path

from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Vertical
from textual.screen import Screen
from textual.widgets import Header, Input, Static, OptionList
from textual.widgets.option_list import Option

from .grid import GRID_SIZE, hand_at, RANKS
from . import range_io


def _selection_rect(ar: int, ac: int, cr: int, cc: int) -> tuple[int, int, int, int]:
    r1, r2 = min(ar, cr), max(ar, cr)
    c1, c2 = min(ac, cc), max(ac, cc)
    return r1, c1, r2, c2


def _cells_in_selection(ar: int, ac: int, cr: int, cc: int) -> list[tuple[int, int]]:
    r1, c1, r2, c2 = _selection_rect(ar, ac, cr, cc)
    return [(r, c) for r in range(r1, r2 + 1) for c in range(c1, c2 + 1)]


# Key layout: 0 hjkl, 1 jikl (j left i up k down l right), 2 wasd
_LAYOUT_KEYS = (
    {"left": ("h",), "right": ("l",), "up": ("k",), "down": ("j",)},
    {"left": ("j",), "right": ("l",), "up": ("i",), "down": ("k",)},
    {"left": ("a",), "right": ("d",), "up": ("w",), "down": ("s",)},
)
_LAYOUT_NAMES = ("hjkl", "jikl", "wasd")
_DIRS = {"left": (0, -1), "right": (0, 1), "up": (-1, 0), "down": (1, 0)}


def _red_style(weight: float) -> str:
    """Rich style: 0 = no red; 0.1–1.0 scale linearly from light to deep red (1 lighter than 5, 5 deeper than 3)."""
    if weight <= 0:
        return ""
    t = min(1.0, max(0.0, weight))  # 0.1 → light, 0.5 → deeper, 1.0 → deepest
    r = int(255 - t * (255 - 139))
    g = int(220 * (1 - t))
    b = int(220 * (1 - t))
    return f"[#{r:02x}{g:02x}{b:02x}]"


class RangeGridView(Static):
    """Renders the 13x13 hand grid with weights and selection."""

    def __init__(self, get_state: callable, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self._get_state = get_state

    def on_mount(self) -> None:
        self.update_interval = 0.1

    def render(self) -> str:
        state = self._get_state()
        weights = state["weights"]
        cr, cc = state["cursor_row"], state["cursor_col"]
        ar, ac = state["anchor_row"], state["anchor_col"]
        selecting = state["selecting"]
        sel_cells = set(_cells_in_selection(ar, ac, cr, cc)) if selecting else {(cr, cc)}

        lines: list[str] = []
        # Header row: column labels A -> 2 (rank = 12 - col)
        header = "    " + " ".join(f"{RANKS[12 - c]:^3}" for c in range(GRID_SIZE))
        lines.append(header)
        for row in range(GRID_SIZE):
            rank = hand_at(row, 0)[0] if row == 0 else hand_at(row, row)[0]
            parts = [f" {rank} "]
            for col in range(GRID_SIZE):
                hand = hand_at(row, col)
                w = weights[row][col]
                in_sel = (row, col) in sel_cells
                is_cursor = (row, col) == (cr, cc)
                if is_cursor:
                    cell = f"[bold reverse]{hand:^3}[/]"
                elif in_sel:
                    cell = f"[reverse]{hand:^3}[/]"
                else:
                    red = _red_style(w)
                    cell = f"{red}{hand:^3}[/]" if red else f"{hand:^3}"
                parts.append(cell)
            lines.append(" ".join(parts))
        return "\n".join(lines)


class RangeGridScreen(Screen[None]):
    """Main screen: 13x13 grid, cursor, shift-select, set weight."""

    # Movement (h/j/k/l, arrows, shift+extend) handled only in on_key to avoid double-handling / skip
    BINDINGS = [
        Binding("q", "quit", "Quit"),
        Binding("ctrl+q", "quit", "Quit"),
        Binding("escape", "clear_selection", "Clear sel", show=False),
        Binding("o", "load", "Load"),
        Binding("c", "clear_grid", "Clear"),
        Binding("m", "cycle_layout", "Layout"),
    ]

    def __init__(self) -> None:
        super().__init__()
        self.weights: list[list[float]] = [[0.0] * GRID_SIZE for _ in range(GRID_SIZE)]
        self.cursor_row = 0
        self.cursor_col = 0
        self.anchor_row = 0
        self.anchor_col = 0
        self.selecting = False
        self._grid_view: RangeGridView | None = None
        self._key_layout = 2  # 0 hjkl, 1 jikl, 2 wasd (wasd default)
        self._current_range_path: str | None = None  # relative path under data/ranges (no .json)

    def _get_state(self) -> dict:
        return {
            "weights": self.weights,
            "cursor_row": self.cursor_row,
            "cursor_col": self.cursor_col,
            "anchor_row": self.anchor_row,
            "anchor_col": self.anchor_col,
            "selecting": self.selecting,
        }

    def _selected_cells(self) -> list[tuple[int, int]]:
        if self.selecting:
            return _cells_in_selection(self.anchor_row, self.anchor_col, self.cursor_row, self.cursor_col)
        return [(self.cursor_row, self.cursor_col)]

    def _move(self, dr: int, dc: int, extend: bool) -> None:
        if extend:
            if not self.selecting:
                self.anchor_row, self.anchor_col = self.cursor_row, self.cursor_col
                self.selecting = True
        else:
            self.selecting = False
        self.cursor_row = max(0, min(GRID_SIZE - 1, self.cursor_row + dr))
        self.cursor_col = max(0, min(GRID_SIZE - 1, self.cursor_col + dc))
        if self._grid_view:
            self._grid_view.refresh()
        self._update_footer()

    def _set_weight(self, value: float) -> None:
        for r, c in self._selected_cells():
            self.weights[r][c] = value
        if self._grid_view:
            self._grid_view.refresh()
        self._update_footer()

    def _update_footer(self) -> None:
        cells = self._selected_cells()
        hand = hand_at(self.cursor_row, self.cursor_col)
        w = self.weights[self.cursor_row][self.cursor_col]
        layout_name = _LAYOUT_NAMES[self._key_layout]
        file_info = f"Editing: {self._current_range_path}.json  " if self._current_range_path else "New range  "
        base = (
            f" {file_info}{layout_name} move  Shift+extend  0-9/F set weight  "
            f"v save  o load  c clear  m layout  Quit: ctrl+q (or q)"
        )
        if len(cells) > 1:
            self.query_one("#status", Static).update(
                f" {len(cells)} cells | {hand} {int(w*100)}% | {base}"
            )
        else:
            self.query_one("#status", Static).update(f" {hand} {int(w*100)}% | {base}")

    def compose(self) -> ComposeResult:
        self._grid_view = RangeGridView(self._get_state)
        yield Header(show_clock=False)
        with Vertical():
            yield self._grid_view
            yield Static("", id="status")

    def on_mount(self) -> None:
        self._update_footer()

    def on_key(self, event: "Message") -> None:
        key = getattr(event, "key", None)
        if not key:
            return
        # Number keys 0-9 -> set weight
        if key in "0123456789":
            event.prevent_default()
            self._set_weight(int(key) / 10.0)
            return
        # Full (100%): f or F so WASD users can use F without triggering save
        if key in ("f", "F"):
            event.prevent_default()
            self._set_weight(1.0)
            return
        # Arrows (all layouts)
        if key == "left":
            event.prevent_default()
            self._move(0, -1, False)
            return
        if key == "right":
            event.prevent_default()
            self._move(0, 1, False)
            return
        if key == "up":
            event.prevent_default()
            self._move(-1, 0, False)
            return
        if key == "down":
            event.prevent_default()
            self._move(1, 0, False)
            return
        if key in ("shift+left", "shift+right", "shift+up", "shift+down"):
            event.prevent_default()
            dr = -1 if key == "shift+up" else 1 if key == "shift+down" else 0
            dc = -1 if key == "shift+left" else 1 if key == "shift+right" else 0
            self._move(dr, dc, True)
            return
        # Layout-specific movement (m toggles hjkl / jikl / wasd)
        layout = _LAYOUT_KEYS[self._key_layout]
        key_normal = key.replace("shift+", "").lower() if "shift+" in key else key.lower()
        extend = key.startswith("shift+") or (len(key) == 1 and key != key.lower())
        for direction, keys in layout.items():
            if key_normal in keys or key in keys:
                event.prevent_default()
                dr, dc = _DIRS[direction]
                self._move(dr, dc, extend)
                return
        if key == "m":
            event.prevent_default()
            self._key_layout = (self._key_layout + 1) % 3
            self._update_footer()
            self.app.notify(f"Layout: {_LAYOUT_NAMES[self._key_layout]}")
            return
        # v = save (default in all layouts)
        if key == "v":
            event.prevent_default()
            self.action_save()
            return
        # Hold Shift + j/k = open save (move range to folder)
        if key in ("shift+j", "shift+k"):
            event.prevent_default()
            self.action_save()
            return
        if key == "escape":
            event.prevent_default()
            self.selecting = False
            if self._grid_view:
                self._grid_view.refresh()
            self._update_footer()
            return
        if key in ("q", "ctrl+q"):
            event.prevent_default()
            self.app.exit()
            return

    def action_move_left(self) -> None:
        self._move(0, -1, False)

    def action_move_right(self) -> None:
        self._move(0, 1, False)

    def action_move_up(self) -> None:
        self._move(-1, 0, False)

    def action_move_down(self) -> None:
        self._move(1, 0, False)

    def action_extend_left(self) -> None:
        self._move(0, -1, True)

    def action_extend_right(self) -> None:
        self._move(0, 1, True)

    def action_extend_up(self) -> None:
        self._move(-1, 0, True)

    def action_extend_down(self) -> None:
        self._move(1, 0, True)

    def action_clear_selection(self) -> None:
        self.selecting = False
        if self._grid_view:
            self._grid_view.refresh()
        self._update_footer()

    def action_save(self) -> None:
        self.app.push_screen(
            SaveRangeScreen(data=self._build_range_data(), current_range_path=self._current_range_path),
            self._on_save_done,
        )

    def _on_save_done(self, result: str | bool) -> None:
        if result is True:
            return  # legacy
        if result:
            self._current_range_path = result
            self._update_footer()
            self.app.notify(f"Saved {result}.json")

    def action_load(self) -> None:
        self.app.push_screen(LoadRangeScreen(), self._on_load_done)

    def _on_load_done(self, result: dict | tuple[dict, str] | None) -> None:
        if result is None:
            return
        if isinstance(result, tuple) and len(result) == 2:
            data, path = result
            self.load_range_into_grid(data)
            self._current_range_path = path
            self._update_footer()
            self.app.notify(f"Loaded {path}.json")
        else:
            self.load_range_into_grid(result)
            self._current_range_path = None
            self._update_footer()
            self.app.notify("Range loaded")

    def action_clear_grid(self) -> None:
        self.weights = [[0.0] * GRID_SIZE for _ in range(GRID_SIZE)]
        self.selecting = False
        self._current_range_path = None
        if self._grid_view:
            self._grid_view.refresh()
        self._update_footer()
        self.notify("Grid cleared")

    def _build_range_data(self) -> dict:
        hands = {}
        for row in range(GRID_SIZE):
            for col in range(GRID_SIZE):
                h = hand_at(row, col)
                w = self.weights[row][col]
                if w > 0:
                    hands[h] = round(w, 4)
        return {"name": "", "description": "", "hands": hands}

    def load_range_into_grid(self, data: dict) -> None:
        hands = data.get("hands", {})
        for row in range(GRID_SIZE):
            for col in range(GRID_SIZE):
                h = hand_at(row, col)
                self.weights[row][col] = float(hands.get(h, 0))
        if self._grid_view:
            self._grid_view.refresh()
        self._update_footer()


class SaveRangeScreen(Screen[str | bool]):
    """Browse data/ranges/; s = save to current file, f = save as (filename), n = new folder."""

    BINDINGS = [
        Binding("escape", "cancel", "Back"),
        Binding("q", "cancel", "Back"),
        Binding("n", "new_folder", "New folder"),
        Binding("f", "focus_filename", "Save as (f)"),
    ]

    def __init__(self, *, data: dict, current_range_path: str | None = None) -> None:
        super().__init__()
        self._data = data
        self._current_range_path: str | None = current_range_path
        self._current_dir: Path = range_io.RANGES_DIR
        self._listing: list[tuple[str, bool]] = []

    def compose(self) -> ComposeResult:
        hint = "s save current  " if self._current_range_path else ""
        yield Static(f"Save: {hint}j/k move Enter · (new folder) n · Save as (f)", id="save_title")
        yield OptionList(id="save_filelist")
        yield Static("(new folder) — type here:", id="save_new_folder_label")
        yield Input(placeholder="New folder name, Enter to create · Escape cancel", id="save_new_folder_input")
        yield Static("Save as (f) — type filename:", id="save_filename_label")
        yield Input(placeholder="e.g. btn_open_50, Enter to save", id="save_filename")
        yield Static("", id="save_msg")

    def on_mount(self) -> None:
        range_io._ensure_ranges_dir()
        self._current_dir = range_io.RANGES_DIR
        self._refresh_list()
        self.query_one("#save_filelist", OptionList).focus()

    def _refresh_list(self) -> None:
        self._listing = []
        if self._current_dir != range_io.RANGES_DIR:
            self._listing.append(("..", True))
        for name, is_dir in range_io.list_entries(self._current_dir):
            self._listing.append((name, is_dir))
        self._listing.append(("(new folder)", False))
        self._listing.append(("Save as (f)", False))
        opt = self.query_one("#save_filelist", OptionList)
        opt.clear_options()
        display = []
        for name, is_dir in self._listing:
            if is_dir and name not in ("(new folder)", "Save as (f)", ".."):
                display.append(f"  {name}/")
            else:
                display.append(name)
        opt.add_options([Option(d) for d in display])
        try:
            rel = self._current_dir.relative_to(range_io.RANGES_DIR)
            path_str = str(rel) if rel.parts else "."
        except ValueError:
            path_str = "."
        hint = "s save current  " if self._current_range_path else ""
        self.query_one("#save_title", Static).update(
            f"Save: data/ranges/{path_str}  {hint}j/k move · (new folder) n · Save as (f)"
        )

    def on_key(self, event: object) -> None:
        key = getattr(event, "key", None)
        if not key:
            return
        inp_new = self.query_one("#save_new_folder_input", Input)
        inp_name = self.query_one("#save_filename", Input)
        focused = self.focused
        fid = getattr(focused, "id", None) if focused else None
        if key == "escape":
            if fid == "save_new_folder_input":
                event.prevent_default()
                inp_new.value = ""
                self.query_one("#save_filelist", OptionList).focus()
                return
            if fid == "save_filename":
                event.prevent_default()
                inp_name.value = ""
                self.query_one("#save_filelist", OptionList).focus()
                return
        if fid in ("save_new_folder_input", "save_filename"):
            return
        opt = self.query_one("#save_filelist", OptionList)
        if key in ("j", "down"):
            event.prevent_default()
            opt.action_cursor_down()
            return
        if key in ("k", "up"):
            event.prevent_default()
            opt.action_cursor_up()
            return
        if key in ("l", "right"):
            event.prevent_default()
            self._activate_selection()
            return
        if key == "f":
            event.prevent_default()
            self.query_one("#save_filename", Input).focus()
            return
        if key == "s" and self._current_range_path:
            event.prevent_default()
            self._save_current_file()
            return

    def _save_current_file(self) -> None:
        if not self._current_range_path:
            return
        if range_io.save_range(self._current_range_path, self._data):
            self.dismiss(self._current_range_path)
            self.app.notify(f"Saved {self._current_range_path}.json")
        else:
            self.query_one("#save_msg", Static).update("Save failed")

    def _activate_selection(self) -> None:
        opt = self.query_one("#save_filelist", OptionList)
        idx = opt.highlighted if opt.highlighted is not None else 0
        if idx < 0 or idx >= len(self._listing):
            return
        name, is_dir = self._listing[idx]
        if name == "..":
            self._current_dir = self._current_dir.parent
            self._refresh_list()
            opt.focus()
            return
        if name == "(new folder)":
            self.query_one("#save_new_folder_input", Input).focus()
            return
        if name == "Save as (f)":
            self.query_one("#save_filename", Input).focus()
            return
        if is_dir:
            self._current_dir = self._current_dir / name
            self._refresh_list()
            opt.focus()
            return
        # Selecting a file in save screen does nothing (or could prefill filename)

    def on_option_list_option_selected(self, event: OptionList.OptionSelected) -> None:
        idx = event.option_index
        if idx < 0 or idx >= len(self._listing):
            return
        name, is_dir = self._listing[idx]
        if name == "..":
            self._current_dir = self._current_dir.parent
            self._refresh_list()
            return
        if name == "(new folder)":
            self.query_one("#save_new_folder_input", Input).focus()
            return
        if name == "Save as (f)":
            self.query_one("#save_filename", Input).focus()
            return
        if is_dir:
            self._current_dir = self._current_dir / name
            self._refresh_list()
            return

    def _on_new_folder_submit(self) -> None:
        inp = self.query_one("#save_new_folder_input", Input)
        name = inp.value.strip()
        inp.value = ""
        self.query_one("#save_filelist", OptionList).focus()
        if not name:
            return
        if range_io.create_folder(self._current_dir, name):
            self._current_dir = self._current_dir / name
            self._refresh_list()
            self.app.notify("Folder created")
        else:
            self.query_one("#save_msg", Static).update(
                "Could not create (use letters, numbers, _ only; no / or \\)"
            )

    def _do_save(self) -> None:
        inp = self.query_one("#save_filename", Input)
        name = inp.value.strip()
        if not name:
            self.query_one("#save_msg", Static).update("Enter a filename")
            return
        try:
            rel = self._current_dir.relative_to(range_io.RANGES_DIR)
            save_name = f"{rel}/{name}" if rel.parts else name
        except ValueError:
            save_name = name
        self._data["name"] = name
        if range_io.save_range(save_name, self._data):
            self.dismiss(save_name)
            self.app.notify(f"Saved data/ranges/{save_name}.json")
        else:
            self.query_one("#save_msg", Static).update("Save failed")

    def action_new_folder(self) -> None:
        self.query_one("#save_new_folder_input", Input).focus()

    def action_focus_filename(self) -> None:
        self.query_one("#save_filename", Input).focus()

    def on_input_submitted(self, event: Input.Submitted) -> None:
        if event.input.id == "save_new_folder_input":
            self._on_new_folder_submit()
            return
        if event.input.id == "save_filename":
            self._do_save()

    def action_cancel(self) -> None:
        self.dismiss(False)


class LoadRangeScreen(Screen[dict | None]):
    """Browse data/ranges/ (folders + files), j/k or arrows to move, Enter to open/load. Shift+j/k on file = move."""

    BINDINGS = [Binding("escape", "cancel", "Back"), Binding("q", "cancel", "Back")]

    def __init__(self) -> None:
        super().__init__()
        self._current_dir: Path = range_io.RANGES_DIR
        self._listing: list[tuple[str, bool]] = []  # (name, is_dir)
        self._move_pending: tuple[Path, str] | None = None  # (from_dir, filename) when Shift+j/k on a file

    def compose(self) -> ComposeResult:
        yield Static("Load: j/k move Enter open · Shift+j/k on file = move to folder", id="load_title")
        yield OptionList(id="filelist")
        yield Static("", id="msg")

    def on_mount(self) -> None:
        range_io._ensure_ranges_dir()
        self._current_dir = range_io.RANGES_DIR
        self._refresh_list()
        opt = self.query_one("#filelist", OptionList)
        if opt.option_count:
            opt.focus()

    def _refresh_list(self) -> None:
        self._listing = []
        if self._current_dir != range_io.RANGES_DIR:
            self._listing.append(("..", True))
        for name, is_dir in range_io.list_entries(self._current_dir):
            self._listing.append((name, is_dir))
        opt = self.query_one("#filelist", OptionList)
        opt.clear_options()
        display = []
        for name, is_dir in self._listing:
            if is_dir and name != "..":
                display.append(f"  {name}/")
            else:
                display.append(name)
        opt.add_options([Option(d) for d in display])
        try:
            rel = self._current_dir.relative_to(range_io.RANGES_DIR)
            path_str = str(rel) if rel.parts else "."
        except ValueError:
            path_str = "."
        if self._move_pending:
            _, fname = self._move_pending
            self.query_one("#load_title", Static).update(
                f"Move '{fname}' → data/ranges/{path_str}  Enter confirm · Escape cancel"
            )
        else:
            self.query_one("#load_title", Static).update(
                f"Load: data/ranges/{path_str}  j/k move Enter open · Shift+j/k on file = move"
            )

    def on_key(self, event: "object") -> None:
        key = getattr(event, "key", None)
        if not key:
            return
        if key == "escape" and self._move_pending:
            event.prevent_default()
            self._move_pending = None
            self._refresh_list()
            return
        opt = self.query_one("#filelist", OptionList)
        idx = opt.highlighted if opt.highlighted is not None else 0
        # Shift+j/k on a file = start move (move file to folder)
        if key in ("shift+j", "shift+k") and 0 <= idx < len(self._listing):
            name, is_dir = self._listing[idx]
            if not is_dir and name.endswith(".json"):
                event.prevent_default()
                self._move_pending = (self._current_dir, name)
                self._refresh_list()
                return
        # Enter when move pending = confirm move to current folder
        if self._move_pending and key == "enter":
            event.prevent_default()
            from_dir, fname = self._move_pending
            if range_io.move_file(from_dir, fname, self._current_dir):
                self.app.notify(f"Moved {fname} to folder")
            else:
                self.query_one("#msg", Static).update("Move failed")
            self._move_pending = None
            self._refresh_list()
            return
        if key in ("j", "down"):
            event.prevent_default()
            opt.action_cursor_down()
            return
        if key in ("k", "up"):
            event.prevent_default()
            opt.action_cursor_up()
            return
        if key in ("l", "right"):
            event.prevent_default()
            self._activate_selection()
            return

    def _activate_selection(self) -> None:
        if self._move_pending:
            return  # Enter already handled in on_key
        opt = self.query_one("#filelist", OptionList)
        idx = opt.highlighted if opt.highlighted is not None else 0
        if idx < 0 or idx >= len(self._listing):
            return
        name, is_dir = self._listing[idx]
        if name == "..":
            self._current_dir = self._current_dir.parent
            self._refresh_list()
            if opt.option_count:
                opt.focus()
            return
        if is_dir:
            self._current_dir = self._current_dir / name
            self._refresh_list()
            if opt.option_count:
                opt.focus()
            return
        data = range_io.load_range(self._current_dir / name)
        if data:
            try:
                rel = self._current_dir.relative_to(range_io.RANGES_DIR)
                path_str = f"{rel}/{name}" if rel.parts else name
            except ValueError:
                path_str = name
            path_str = path_str.removesuffix(".json")
            self.dismiss((data, path_str))
        else:
            self.query_one("#msg", Static).update("Load failed")

    def on_option_list_option_selected(self, event: OptionList.OptionSelected) -> None:
        if self._move_pending:
            return
        idx = event.option_index
        if idx < 0 or idx >= len(self._listing):
            return
        name, is_dir = self._listing[idx]
        if name == "..":
            self._current_dir = self._current_dir.parent
            self._refresh_list()
            return
        if is_dir:
            self._current_dir = self._current_dir / name
            self._refresh_list()
            return
        data = range_io.load_range(self._current_dir / name)
        if data:
            try:
                rel = self._current_dir.relative_to(range_io.RANGES_DIR)
                path_str = f"{rel}/{name}" if rel.parts else name
            except ValueError:
                path_str = name
            path_str = path_str.removesuffix(".json")
            self.dismiss((data, path_str))
        else:
            self.query_one("#msg", Static).update("Load failed")

    def action_cancel(self) -> None:
        self.dismiss(None)


class RangefinderApp(App[None]):
    """Rangefinder TUI app."""

    TITLE = "Rangefinder"
    CSS = """
    #placeholder {
        display: none;
    }
    RangeGridView {
        padding: 1 2;
        min-height: 20;
        height: auto;
    }
    #status {
        padding: 0 1;
        color: $text-muted;
    }
    """

    def on_mount(self) -> None:
        range_io._ensure_ranges_dir()
        self.push_screen(RangeGridScreen())

    def compose(self) -> ComposeResult:
        # Main content is shown via push_screen(RangeGridScreen()) in on_mount
        # so the grid screen is the active screen, not a child of the default screen.
        yield Static("Loading…", id="placeholder")

