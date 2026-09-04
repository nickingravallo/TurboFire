"""NashGPT 1.7 train-time augmentation and sampling utilities."""

from __future__ import annotations

import itertools

import numpy as np

from card_tokens import SUITS, TOKEN_FORMAT_SPLIT, permutation_lookup
from legal import KIND_FACE_BET
from tokenizer import Tokenizer

REPRESENTATION_VERSION = "nashgpt17-v1"
IDENTITY_PERM = tuple(range(len(SUITS)))
NON_IDENTITY_PERMS = tuple(
    perm
    for perm in itertools.permutations(range(len(SUITS)))
    if perm != IDENTITY_PERM
)


def suit_id_lookups(tokenizer: Tokenizer) -> np.ndarray:
    """Return one vocab-id lookup row per non-identity global suit permutation."""
    if tokenizer.token_format != TOKEN_FORMAT_SPLIT:
        raise ValueError("suit permutation requires split rank/suit tokens")
    base = np.arange(tokenizer.vocab_size, dtype=np.int64)
    rows = []
    for perm in NON_IDENTITY_PERMS:
        row = base.copy()
        for old_id, new_id in permutation_lookup(tokenizer.token_to_id, perm).items():
            row[old_id] = new_id
        rows.append(row)
    return np.stack(rows)


def permute_batch_suits(
    x: np.ndarray,
    lookups: np.ndarray,
    rng: np.random.Generator,
    probability: float,
) -> np.ndarray:
    """Apply independent global suit relabelings to selected rows."""
    if probability <= 0.0 or x.size == 0:
        return x
    if not 0.0 <= probability <= 1.0:
        raise ValueError("suit permutation probability must be in [0, 1]")
    out = np.array(x, copy=True)
    selected = rng.random(len(out)) < probability
    rows = np.flatnonzero(selected)
    if rows.size:
        choices = rng.integers(0, len(lookups), size=rows.size)
        for row, choice in zip(rows, choices):
            out[row] = lookups[choice][out[row]]
    return out


def sampling_weights(
    target_probs: np.ndarray,
    kinds: np.ndarray,
    facing_bet_weight: float,
    entropy_weight: float,
) -> np.ndarray:
    """Weights ``a_kind * (1 + entropy_weight * H(q))``."""
    if facing_bet_weight <= 0.0 or entropy_weight < 0.0:
        raise ValueError("sampling weights must be non-negative")
    probs = np.asarray(target_probs, dtype=np.float64)
    entropy = -(probs * np.log(np.clip(probs, 1e-12, 1.0))).sum(axis=1)
    weights = 1.0 + entropy_weight * entropy
    weights = np.asarray(weights, dtype=np.float64)
    weights[np.asarray(kinds) == KIND_FACE_BET] *= facing_bet_weight
    return weights
