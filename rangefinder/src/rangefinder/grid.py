"""13x13 hand grid model. Row/col mapping matches solver: pairs on diagonal, suited above, offsuit below."""

RANKS = "23456789TJQKA"  # index 0 = 2, 12 = A
GRID_SIZE = 13


def hand_at(row: int, col: int) -> str:
    """Return hand string (e.g. AA, AKs, AKo) for grid cell (row, col)."""
    row = max(0, min(GRID_SIZE - 1, row))
    col = max(0, min(GRID_SIZE - 1, col))
    r_high = 12 - row
    r_low = 12 - col
    if row == col:
        return f"{RANKS[r_high]}{RANKS[r_high]}"  # pair
    if row < col:
        return f"{RANKS[r_high]}{RANKS[r_low]}s"  # suited
    # offsuit: high rank from col, low from row (solver: Hand(12-col, 12-row, false))
    r_high = 12 - col
    r_low = 12 - row
    return f"{RANKS[r_high]}{RANKS[r_low]}o"


def row_col_for_hand(hand: str) -> tuple[int, int] | None:
    """Return (row, col) for a hand string, or None if invalid."""
    hand = hand.strip().upper()
    if len(hand) < 2:
        return None
    try:
        r1 = RANKS.index(hand[0])
        r2 = RANKS.index(hand[1])
    except ValueError:
        return None
    suited: bool | None = None
    if len(hand) >= 3:
        if hand[2] == "S":
            suited = True
        elif hand[2] == "O":
            suited = False
    high, low = max(r1, r2), min(r1, r2)
    row = 12 - high
    col = 12 - low
    if high == low:
        return (row, row)
    if suited is True:
        return (row, col)
    if suited is False:
        return (col, row)
    return None  # ambiguous


def all_hand_strings() -> list[str]:
    """Return all 169 hand strings in grid order (row-major)."""
    out: list[str] = []
    for row in range(GRID_SIZE):
        for col in range(GRID_SIZE):
            out.append(hand_at(row, col))
    return out
