"""Legal HERO_* sets matching src/dcfr.c get_legal_actions.

Default flop abstraction:
  not facing a bet → CHECK + BET10/25/52/100/123
  facing a bet     → FOLD + CALL + RAISE_2X/3X/4X
  facing a bet and raises_this_street >= max_raises (2) → FOLD + CALL

NashGPT 1.6 abstraction:
  not facing a bet → CHECK + BET40/BET100 + ALLIN
  facing a bet     → FOLD + CALL + RAISE_3X + ALLIN
  facing a raise/all-in → FOLD + CALL
"""

from __future__ import annotations

from typing import Iterable

import numpy as np

from tokenizer import HERO_PREFIX, PAD, Tokenizer

PROFILE_DEFAULT = "default"
PROFILE_NASHGPT16 = "nashgpt16"
LEGAL_PROFILES = (PROFILE_DEFAULT, PROFILE_NASHGPT16)

KIND_OPEN = 0
KIND_FACE_BET = 1
KIND_FACE_CAP = 2
NUM_KINDS = 3

PROFILE_ACTION_NAMES: dict[str, tuple[tuple[str, ...], ...]] = {
    PROFILE_DEFAULT: (
        ("CHECK", "BET10", "BET25", "BET52", "BET100", "BET123"),
        ("FOLD", "CALL", "RAISE_2X", "RAISE_3X", "RAISE_4X"),
        ("FOLD", "CALL"),
    ),
    PROFILE_NASHGPT16: (
        ("CHECK", "BET40", "BET100", "ALLIN"),
        ("FOLD", "CALL", "RAISE_3X", "ALLIN"),
        ("FOLD", "CALL"),
    ),
}


def _bare_action(tok: str) -> str:
    if tok.startswith("HERO_") or tok.startswith("OPP_"):
        return tok.split("_", 1)[1]
    return tok


def betting_actions(tokens: Iterable[str]) -> list[str]:
    hist: list[str] = []
    seen = False
    for tok in tokens:
        if not seen:
            if tok == "<BETTING>":
                seen = True
            continue
        if tok == PAD:
            continue
        name = _bare_action(tok)
        if name:
            hist.append(name)
    return hist


def node_kind_from_actions(
    actions: list[str], profile: str = PROFILE_DEFAULT
) -> int:
    if profile not in PROFILE_ACTION_NAMES:
        raise ValueError(f"unknown legal profile: {profile}")
    if not actions:
        return KIND_OPEN
    last = actions[-1]
    facing = last.startswith("BET") or last.startswith("RAISE") or last == "ALLIN"
    if not facing:
        return KIND_OPEN
    if last == "ALLIN":
        return KIND_FACE_CAP
    raises = sum(1 for a in actions if a.startswith("RAISE"))
    max_raises = 1 if profile == PROFILE_NASHGPT16 else 2
    if raises >= max_raises:
        return KIND_FACE_CAP
    return KIND_FACE_BET


def node_kind_from_context(context: str, profile: str = PROFILE_DEFAULT) -> int:
    return node_kind_from_actions(betting_actions(context.split()), profile)


def node_kind_from_ids(
    ids: Iterable[int], tokenizer: Tokenizer, profile: str = PROFILE_DEFAULT
) -> int:
    pad_id = tokenizer.pad_id
    tokens = []
    for i in ids:
        i = int(i)
        if i == pad_id:
            continue
        tokens.append(tokenizer.id_to_token.get(i, ""))
    return node_kind_from_actions(betting_actions(tokens), profile)


def hero_legal_tokens(
    kind: int, profile: str = PROFILE_DEFAULT
) -> tuple[str, ...]:
    names = PROFILE_ACTION_NAMES[profile][int(kind)]
    return tuple(HERO_PREFIX + name for name in names)


def legal_ids_for_kind(
    tokenizer: Tokenizer, kind: int, profile: str = PROFILE_DEFAULT
) -> list[int]:
    ids = []
    for tok in hero_legal_tokens(kind, profile):
        tid = tokenizer.token_to_id.get(tok)
        if tid is not None:
            ids.append(tid)
    return ids


def kind_masks(
    tokenizer: Tokenizer, profile: str = PROFILE_DEFAULT
) -> np.ndarray:
    """(NUM_KINDS, vocab) bool: True iff the token is legal for that node kind."""
    v = tokenizer.vocab_size
    masks = np.zeros((NUM_KINDS, v), dtype=np.bool_)
    for kind in range(NUM_KINDS):
        for tid in legal_ids_for_kind(tokenizer, kind, profile):
            masks[kind, tid] = True
    return masks


def legal_mask_for_context(
    tokenizer: Tokenizer, context: str, profile: str = PROFILE_DEFAULT
) -> np.ndarray:
    masks = kind_masks(tokenizer, profile)
    return masks[node_kind_from_context(context, profile)]
