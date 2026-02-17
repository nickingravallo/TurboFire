"""Load/save range JSON to data/ranges/ folder."""

import json
import shutil
from pathlib import Path
from typing import Any

# Project root: .../rangefinder (parent of src/)
_PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
DATA_DIR = _PROJECT_ROOT / "data"
RANGES_DIR = DATA_DIR / "ranges"


def _ensure_ranges_dir() -> Path:
    RANGES_DIR.mkdir(parents=True, exist_ok=True)
    return RANGES_DIR


def list_ranges() -> list[str]:
    """Return list of range filenames (without path) in data/ranges/ (no subfolders)."""
    if not RANGES_DIR.exists():
        return []
    return sorted(
        f.name for f in RANGES_DIR.iterdir()
        if f.is_file() and f.suffix.lower() == ".json"
    )


def list_entries(directory: Path) -> list[tuple[str, bool]]:
    """List directory: (name, is_dir). Dirs first (sorted), then .json files (sorted)."""
    if not directory.exists() or not directory.is_dir():
        return []
    dirs: list[str] = []
    files: list[str] = []
    for f in directory.iterdir():
        if f.name.startswith("."):
            continue
        if f.is_dir():
            dirs.append(f.name)
        elif f.suffix.lower() == ".json":
            files.append(f.name)
    return [(n, True) for n in sorted(dirs)] + [(n, False) for n in sorted(files)]


def create_folder(parent: Path, name: str) -> bool:
    """Create directory parent/name inside data/ranges/. Returns True on success."""
    if not isinstance(parent, Path):
        return False
    # Keep parent under RANGES_DIR
    try:
        parent = parent.resolve()
        parent.relative_to(RANGES_DIR.resolve())
    except (ValueError, OSError):
        parent = RANGES_DIR
    name = name.strip()
    if not name:
        return False
    # No path separators in name
    if "/" in name or "\\" in name or name in (".", ".."):
        return False
    try:
        parent.mkdir(parents=True, exist_ok=True)
        path = parent / name
        path.mkdir(parents=False, exist_ok=True)
        return True
    except OSError:
        return False


def move_file(from_dir: Path, filename: str, to_dir: Path) -> bool:
    """Move from_dir/filename to to_dir/filename (both under data/ranges/). Returns True on success."""
    if not isinstance(from_dir, Path) or not isinstance(to_dir, Path):
        return False
    try:
        from_dir = from_dir.resolve()
        to_dir = to_dir.resolve()
        from_dir.relative_to(RANGES_DIR.resolve())
        to_dir.relative_to(RANGES_DIR.resolve())
    except (ValueError, OSError):
        return False
    src = from_dir / filename
    dst = to_dir / filename
    if not src.is_file() or src == dst:
        return False
    try:
        to_dir.mkdir(parents=True, exist_ok=True)
        shutil.move(str(src), str(dst))
        return True
    except OSError:
        return False


def load_range(name_or_path: str | Path) -> dict[str, Any] | None:
    """Load range from data/ranges/<name>.json or full path. Returns dict with name, description, hands or None."""
    p = Path(name_or_path)
    if not p.is_absolute():
        p = RANGES_DIR / (str(name_or_path) if str(name_or_path).endswith(".json") else f"{name_or_path}.json")
    if not p.is_file():
        return None
    try:
        data = json.loads(p.read_text(encoding="utf-8"))
        if "hands" not in data:
            data["hands"] = {}
        if "name" not in data:
            data["name"] = p.stem
        if "description" not in data:
            data["description"] = ""
        return data
    except (json.JSONDecodeError, OSError):
        return None


def save_range(name: str, data: dict[str, Any]) -> bool:
    """Save range to data/ranges/<name>.json. name can include subfolders (e.g. btn_open/50bb). Creates dirs if needed."""
    _ensure_ranges_dir()
    name = name.strip()
    if not name:
        return False
    if "/" in name or "\\" in name:
        name = name.replace("\\", "/")
    if not name.endswith(".json"):
        name = f"{name}.json"
    path = RANGES_DIR / name
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(data, indent=2), encoding="utf-8")
        return True
    except OSError:
        return False
