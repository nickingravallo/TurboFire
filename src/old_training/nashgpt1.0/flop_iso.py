"""Suit isomorphism for flops. Must match src/isomorphism.c."""

from __future__ import annotations

RANKS = "23456789TJQKA"
SUITS = "shdc"
NUM_CANONICAL_FLOPS = 1755
_BOARD_SPECIALS = frozenset(
    {"<TURN>", "<RIVER>", "<SHOWDOWN>", "<FOLD_END>", "<END>"}
)


def parse_card(token: str) -> int:
    if len(token) != 2 or token[0] not in RANKS or token[1] not in SUITS:
        raise ValueError(f"not a card token: {token}")
    return SUITS.index(token[1]) * 13 + RANKS.index(token[0])


def card_token(card: int) -> str:
    return f"{RANKS[card % 13]}{SUITS[card // 13]}"


def _sort_key(cards: list[tuple[int, int]]) -> list[tuple[int, int]]:
    return sorted(cards, key=lambda cs: (-cs[0], cs[1]))


def _sort_display(cards: list[int]) -> list[int]:
    return sorted(cards, key=lambda c: (-(c % 13), -c))


def _cmp_key(a: list[tuple[int, int]], b: list[tuple[int, int]]) -> int:
    for (ar, asu), (br, bsu) in zip(a, b):
        if ar != br:
            return ar - br
        if asu != bsu:
            return asu - bsu
    return 0


def _map_card(card: int, perm: list[int]) -> tuple[int, int]:
    return card % 13, perm[card // 13]


def _pack(rank: int, suit: int) -> int:
    return suit * 13 + rank


def canonicalize_flop_cards(
    flop: list[int],
    hole: list[int] | None = None,
) -> tuple[list[int], list[int] | None, list[int]]:
    if len(flop) != 3 or len(set(flop)) != 3:
        raise ValueError("flop must be 3 distinct cards")
    if any(c < 0 or c > 51 for c in flop):
        raise ValueError("flop card out of range")
    if hole is not None:
        if len(hole) != 2 or hole[0] == hole[1]:
            raise ValueError("hole must be 2 distinct cards")
        if any(c < 0 or c > 51 for c in hole):
            raise ValueError("hole card out of range")

    best_perm: list[int] | None = None
    best_flop: list[tuple[int, int]] | None = None
    best_hole: list[tuple[int, int]] | None = None

    for a in range(4):
        for b in range(4):
            if b == a:
                continue
            for c in range(4):
                if c == a or c == b:
                    continue
                d = 6 - a - b - c
                perm = [a, b, c, d]
                mapped_flop = _sort_key([_map_card(card, perm) for card in flop])
                mapped_hole = (
                    _sort_key([_map_card(card, perm) for card in hole])
                    if hole is not None
                    else None
                )
                better = False
                if best_flop is None:
                    better = True
                else:
                    flop_cmp = _cmp_key(mapped_flop, best_flop)
                    if flop_cmp < 0:
                        better = True
                    elif flop_cmp == 0 and mapped_hole is not None:
                        better = _cmp_key(mapped_hole, best_hole) < 0
                if not better:
                    continue
                best_perm = perm
                best_flop = mapped_flop
                best_hole = mapped_hole

    assert best_perm is not None
    flop_out = _sort_display([_pack(*_map_card(card, best_perm)) for card in flop])
    hole_out = (
        _sort_display([_pack(*_map_card(card, best_perm)) for card in hole])
        if hole is not None
        else None
    )
    return flop_out, hole_out, best_perm


def collect_canonical_flops() -> list[list[int]]:
    out: list[list[int]] = []
    for a in range(50):
        for b in range(a + 1, 51):
            for c in range(b + 1, 52):
                flop = [a, b, c]
                canon, _, _ = canonicalize_flop_cards(flop)
                if _sort_display(flop) == canon:
                    out.append(canon)
    return out


def canonicalize_prompt(prompt: str) -> str:
    """Rewrite <HOLE> and <FLOP> cards to the canonical suit frame."""
    toks = prompt.split()
    if not toks or "<TURN>" in toks or "<RIVER>" in toks:
        return prompt

    def take_cards(start: int) -> tuple[list[str], int]:
        cards: list[str] = []
        i = start
        while i < len(toks) and not toks[i].startswith("<"):
            cards.append(toks[i])
            i += 1
        return cards, i

    out: list[str] = []
    i = 0
    hole_toks: list[str] | None = None
    flop_toks: list[str] | None = None
    while i < len(toks):
        tok = toks[i]
        if tok in _BOARD_SPECIALS:
            return prompt
        if tok == "<HOLE>":
            hole_toks, i = take_cards(i + 1)
            out.append(tok)
            out.extend(hole_toks)
            continue
        if tok == "<FLOP>":
            flop_toks, i = take_cards(i + 1)
            out.append(tok)
            out.extend(flop_toks)
            continue
        out.append(tok)
        i += 1

    if flop_toks is None or len(flop_toks) != 3:
        return prompt
    try:
        flop = [parse_card(t) for t in flop_toks]
        hole = [parse_card(t) for t in hole_toks] if hole_toks and len(hole_toks) == 2 else None
        canon_flop, canon_hole, _ = canonicalize_flop_cards(flop, hole)
    except ValueError:
        return prompt

    rewritten: list[str] = []
    i = 0
    while i < len(out):
        tok = out[i]
        if tok == "<HOLE>" and canon_hole is not None:
            rewritten.append(tok)
            rewritten.extend(card_token(c) for c in canon_hole)
            i += 1
            while i < len(out) and not out[i].startswith("<"):
                i += 1
            continue
        if tok == "<FLOP>":
            rewritten.append(tok)
            rewritten.extend(card_token(c) for c in canon_flop)
            i += 1
            while i < len(out) and not out[i].startswith("<"):
                i += 1
            continue
        rewritten.append(tok)
        i += 1
    return " ".join(rewritten)


def self_test() -> None:
    flops = collect_canonical_flops()
    assert len(flops) == NUM_CANONICAL_FLOPS, len(flops)
    rainbow = canonicalize_prompt(
        "<START> <HOLE> Ah Kh <FLOP> Qh Jd 7c <BETTING>"
    )
    rainbow2 = canonicalize_prompt(
        "<START> <HOLE> Ad Kd <FLOP> Qd Jh 7s <BETTING>"
    )
    assert rainbow == rainbow2, (rainbow, rainbow2)
    assert canonicalize_prompt(rainbow) == rainbow
    print(f"flop_iso self-test passed ({NUM_CANONICAL_FLOPS} canonical flops)")


if __name__ == "__main__":
    import sys

    if len(sys.argv) > 1 and sys.argv[1] == "--self-test":
        self_test()
    elif len(sys.argv) > 1 and sys.argv[1] == "--canonical-flops":
        for flop in collect_canonical_flops():
            print("".join(card_token(c) for c in flop))
    else:
        prompt = " ".join(sys.argv[1:]) if len(sys.argv) > 1 else ""
        if not prompt:
            raise SystemExit("usage: flop_iso.py --self-test | --canonical-flops | <prompt>")
        print(canonicalize_prompt(prompt))
