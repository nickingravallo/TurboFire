"""Suit isomorphism for flops. Must match src/isomorphism.c.

Training dumps only the 1,755 canonical flop textures. At inference, map a
live hole+board into that frame, run the net, then map cards back with the
inverse suit permutation. Action mixes (HERO_*) are suit-invariant — do not
relabel them.
"""

from __future__ import annotations

from dataclasses import dataclass

from card_tokens import RANKS, SUITS, fuse_cards, split_cards
NUM_CANONICAL_FLOPS = 1755
IDENTITY_PERM = (0, 1, 2, 3)


def parse_card(token: str) -> int:
    if len(token) != 2 or token[0] not in RANKS or token[1] not in SUITS:
        raise ValueError(f"not a card token: {token}")
    return SUITS.index(token[1]) * 13 + RANKS.index(token[0])


def card_token(card: int) -> str:
    return f"{RANKS[card % 13]}{SUITS[card // 13]}"


def is_card_token(token: str) -> bool:
    try:
        parse_card(token)
        return True
    except ValueError:
        return False


def invert_perm(perm: list[int] | tuple[int, ...]) -> list[int]:
    inv = [0, 0, 0, 0]
    for old, new in enumerate(perm):
        inv[int(new)] = old
    return inv


def map_card(card: int, perm: list[int] | tuple[int, ...]) -> int:
    return int(perm[card // 13]) * 13 + (card % 13)


def apply_suit_perm(text: str, perm: list[int] | tuple[int, ...]) -> str:
    """Relabel every card token. Non-cards (actions, specials) stay put."""
    normalized = " ".join(text.split())
    fused = fuse_cards(normalized)
    was_split = fused != normalized
    out: list[str] = []
    for tok in fused.split():
        if is_card_token(tok):
            out.append(card_token(map_card(parse_card(tok), perm)))
        else:
            out.append(tok)
    mapped = " ".join(out)
    return split_cards(mapped) if was_split else mapped


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


def _take_cards(toks: list[str], start: int) -> tuple[list[str], int]:
    cards: list[str] = []
    i = start
    while i < len(toks) and not toks[i].startswith("<"):
        cards.append(toks[i])
        i += 1
    return cards, i


def _replace_section(toks: list[str], tag: str, cards: list[str]) -> list[str]:
    out: list[str] = []
    i = 0
    while i < len(toks):
        if toks[i] == tag:
            out.append(tag)
            out.extend(cards)
            i += 1
            while i < len(toks) and not toks[i].startswith("<"):
                i += 1
            continue
        out.append(toks[i])
        i += 1
    return out


@dataclass(frozen=True)
class IsoFrame:
    """Round-trip handle: live prompt → canonical → back."""

    raw: str
    canonical: str
    perm: tuple[int, int, int, int]

    @property
    def inverse(self) -> tuple[int, int, int, int]:
        inv = invert_perm(self.perm)
        return (inv[0], inv[1], inv[2], inv[3])

    def decanonicalize(self, text: str) -> str:
        """Map canonical card tokens back to the original suit coloring."""
        if self.perm == IDENTITY_PERM:
            return text
        return apply_suit_perm(text, self.inverse)


def canonicalize_runout(prompt: str) -> IsoFrame:
    """Rewrite hole + flop (+ later board cards) into the canonical suit frame.

    Returns an IsoFrame so callers can run the model on `.canonical` and map
    generated card tokens back with `.decanonicalize(...)`.
    """
    raw_prompt = prompt
    prompt = fuse_cards(prompt)
    toks = prompt.split()
    if not toks:
        return IsoFrame(raw=raw_prompt, canonical=prompt, perm=IDENTITY_PERM)

    hole_toks: list[str] | None = None
    flop_toks: list[str] | None = None
    i = 0
    while i < len(toks):
        tok = toks[i]
        if tok == "<HOLE>":
            hole_toks, i = _take_cards(toks, i + 1)
            continue
        if tok == "<FLOP>":
            flop_toks, i = _take_cards(toks, i + 1)
            continue
        i += 1

    if flop_toks is None or len(flop_toks) != 3:
        return IsoFrame(raw=raw_prompt, canonical=prompt, perm=IDENTITY_PERM)

    try:
        flop = [parse_card(t) for t in flop_toks]
        hole = (
            [parse_card(t) for t in hole_toks]
            if hole_toks and len(hole_toks) == 2
            else None
        )
        canon_flop, canon_hole, perm = canonicalize_flop_cards(flop, hole)
    except ValueError:
        return IsoFrame(raw=raw_prompt, canonical=prompt, perm=IDENTITY_PERM)

    mapped = apply_suit_perm(prompt, perm).split()
    mapped = _replace_section(mapped, "<FLOP>", [card_token(c) for c in canon_flop])
    if canon_hole is not None:
        mapped = _replace_section(mapped, "<HOLE>", [card_token(c) for c in canon_hole])
    return IsoFrame(
        raw=raw_prompt,
        canonical=" ".join(mapped),
        perm=(perm[0], perm[1], perm[2], perm[3]),
    )


def canonicalize_prompt(prompt: str) -> str:
    """Rewrite <HOLE> and <FLOP> cards to the canonical suit frame."""
    return canonicalize_runout(prompt).canonical


def decanonicalize_prompt(canonical: str, perm: list[int] | tuple[int, ...]) -> str:
    """Undo a suit permutation. `perm` is the forward map (old_suit → new_suit)."""
    return apply_suit_perm(canonical, invert_perm(perm))


def format_perm(perm: list[int] | tuple[int, ...]) -> str:
    return " ".join(f"{SUITS[old]}->{SUITS[int(new)]}" for old, new in enumerate(perm))


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

    raw = "<START> <HOLE> Ah Kh <FLOP> Qh Jd 7c <BETTING>"
    frame = canonicalize_runout(raw)
    assert frame.canonical != raw
    restored = frame.decanonicalize(frame.canonical)
    restored_cards = [t for t in restored.split() if is_card_token(t)]
    raw_cards = [t for t in raw.split() if is_card_token(t)]
    assert sorted(restored_cards) == sorted(raw_cards), (restored, raw)
    # Action tokens must survive the round-trip unchanged.
    mix = frame.decanonicalize("HERO_CHECK HERO_BET100")
    assert mix == "HERO_CHECK HERO_BET100"
    # Later board cards follow the same perm.
    with_turn = canonicalize_runout(
        "<START> <HOLE> Ah Kh <FLOP> Qh Jd 7c <BETTING> HERO_CHECK <TURN> 2h"
    )
    assert "<TURN>" in with_turn.canonical
    back = with_turn.decanonicalize(with_turn.canonical)
    assert "2h" in back.split()
    print(f"flop_iso self-test passed ({NUM_CANONICAL_FLOPS} canonical flops)")


if __name__ == "__main__":
    import argparse

    p = argparse.ArgumentParser(
        description="Canonicalize / deconvert TurboFire hole+flop prompts"
    )
    p.add_argument("prompt", nargs="*", help="space-separated prompt tokens")
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--canonical-flops", action="store_true")
    p.add_argument(
        "--canonicalize",
        action="store_true",
        help="print canonical prompt (default if a prompt is given)",
    )
    p.add_argument(
        "--decanonicalize",
        action="store_true",
        help="map canonical cards back; requires --perm",
    )
    p.add_argument(
        "--perm",
        type=str,
        default="",
        help="forward suit perm as 4 ints, e.g. 0,2,1,3 (s,h,d,c → new suits)",
    )
    p.add_argument(
        "--roundtrip",
        action="store_true",
        help="print raw, canonical, perm, and deconverted prompt",
    )
    args = p.parse_args()

    if args.self_test:
        self_test()
    elif args.canonical_flops:
        for flop in collect_canonical_flops():
            print("".join(card_token(c) for c in flop))
    else:
        prompt = " ".join(args.prompt)
        if not prompt:
            raise SystemExit(
                "usage: flop_iso.py --self-test | --canonical-flops | "
                "[--roundtrip|--decanonicalize --perm 0,1,2,3] <prompt>"
            )
        if args.decanonicalize:
            if not args.perm:
                raise SystemExit("--decanonicalize requires --perm 0,1,2,3")
            perm = [int(x) for x in args.perm.split(",")]
            if len(perm) != 4:
                raise SystemExit("--perm must be four comma-separated ints")
            print(decanonicalize_prompt(prompt, perm))
        elif args.roundtrip:
            frame = canonicalize_runout(prompt)
            print(f"raw: {frame.raw}")
            print(f"canonical: {frame.canonical}")
            print(f"perm: {format_perm(frame.perm)}")
            print(f"deconverted: {frame.decanonicalize(frame.canonical)}")
        else:
            frame = canonicalize_runout(prompt)
            print(frame.canonical)
            if frame.canonical != frame.raw:
                print(f"perm: {format_perm(frame.perm)}", file=__import__("sys").stderr)
