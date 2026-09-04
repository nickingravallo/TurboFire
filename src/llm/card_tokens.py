"""Versioned card-token representations shared by training and inference."""

from __future__ import annotations

from collections.abc import Iterable, Sequence

RANKS = "23456789TJQKA"
SUITS = "shdc"
TOKEN_FORMAT_FUSED = "fused"
TOKEN_FORMAT_SPLIT = "split_rank_suit"
TOKEN_FORMATS = (TOKEN_FORMAT_FUSED, TOKEN_FORMAT_SPLIT)


def is_fused_card(token: str) -> bool:
    return len(token) == 2 and token[0] in RANKS and token[1] in SUITS


def split_cards(text: str) -> str:
    """Convert ``As`` card atoms to ``A s`` rank/suit tokens."""
    out: list[str] = []
    for token in text.split():
        if is_fused_card(token):
            out.extend(token)
        else:
            out.append(token)
    return " ".join(out)


def fuse_cards(text: str) -> str:
    """Convert adjacent rank/suit tokens to fused card atoms."""
    tokens = text.split()
    out: list[str] = []
    i = 0
    while i < len(tokens):
        if (
            i + 1 < len(tokens)
            and tokens[i] in RANKS
            and tokens[i + 1] in SUITS
        ):
            out.append(tokens[i] + tokens[i + 1])
            i += 2
        else:
            out.append(tokens[i])
            i += 1
    return " ".join(out)


def normalize_context(text: str, token_format: str) -> str:
    if token_format == TOKEN_FORMAT_FUSED:
        return fuse_cards(text)
    if token_format == TOKEN_FORMAT_SPLIT:
        return split_cards(text)
    raise ValueError(f"unknown token format: {token_format}")


def section_cards(context: str, tag: str, count: int) -> list[str]:
    """Read fused or split cards after a section marker, returning fused cards."""
    tokens = context.split()
    try:
        i = tokens.index(tag) + 1
    except ValueError as exc:
        raise ValueError(f"missing section marker: {tag}") from exc

    cards: list[str] = []
    while i < len(tokens) and len(cards) < count:
        token = tokens[i]
        if is_fused_card(token):
            cards.append(token)
            i += 1
        elif (
            token in RANKS
            and i + 1 < len(tokens)
            and tokens[i + 1] in SUITS
        ):
            cards.append(token + tokens[i + 1])
            i += 2
        else:
            break
    if len(cards) != count:
        raise ValueError(f"expected {count} cards after {tag}, found {len(cards)}")
    return cards


def permutation_lookup(
    token_to_id: dict[str, int], perm: Sequence[int]
) -> dict[int, int]:
    """Map split suit-token ids through ``perm[old_suit] -> new_suit``."""
    if sorted(int(p) for p in perm) != list(range(len(SUITS))):
        raise ValueError(f"not a suit permutation: {perm}")
    return {
        token_to_id[suit]: token_to_id[SUITS[int(perm[index])]]
        for index, suit in enumerate(SUITS)
    }


def permute_split_ids(
    ids: Iterable[int], token_to_id: dict[str, int], perm: Sequence[int]
) -> list[int]:
    lookup = permutation_lookup(token_to_id, perm)
    return [lookup.get(int(token_id), int(token_id)) for token_id in ids]
