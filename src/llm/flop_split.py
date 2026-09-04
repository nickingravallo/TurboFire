"""Train / val split by flop texture (not random rows)."""

from __future__ import annotations

from pathlib import Path

import numpy as np

from card_tokens import TOKEN_FORMAT_SPLIT
from tokenizer import Tokenizer

FLOP_TOK = "<FLOP>"


def flop_card_ids(x: np.ndarray, tokenizer: Tokenizer, chunk: int = 250_000) -> np.ndarray:
    """Token ids representing the three cards after ``<FLOP>``."""
    n, t = int(x.shape[0]), int(x.shape[1])
    width = 6 if tokenizer.token_format == TOKEN_FORMAT_SPLIT else 3
    out = np.zeros((n, width), dtype=np.int32)
    if n == 0:
        return out
    if FLOP_TOK not in tokenizer.token_to_id:
        raise ValueError(f"tokenizer is missing {FLOP_TOK}")
    flop_id = tokenizer.token_to_id[FLOP_TOK]
    for start in range(0, n, chunk):
        end = min(n, start + chunk)
        block = np.asarray(x[start:end])
        matches = block == flop_id
        has = matches.any(axis=1)
        pos = matches.argmax(axis=1)
        complete = has & (pos + width < t)
        if not complete.all():
            bad = start + int(np.flatnonzero(~complete)[0])
            raise ValueError(
                f"row {bad} does not contain a complete flop; increase block size"
            )
        idx = np.arange(end - start)
        for k in range(width):
            col = np.clip(pos + 1 + k, 0, t - 1)
            out[start:end, k] = np.where(has, block[idx, col], 0)
    return out


def flop_strings(card_ids: np.ndarray, tokenizer: Tokenizer) -> list[str]:
    rows: list[str] = []
    for encoded in card_ids:
        tokens = [tokenizer.id_to_token.get(int(c), "<UNK>") for c in encoded]
        if tokenizer.token_format == TOKEN_FORMAT_SPLIT:
            cards = [
                tokens[index] + tokens[index + 1]
                for index in range(0, len(tokens), 2)
            ]
            rows.append(" ".join(cards))
        else:
            rows.append(" ".join(tokens))
    return rows


def split_by_flop(
    x: np.ndarray,
    tokenizer: Tokenizer,
    seed: int,
    holdout_flops: int,
) -> tuple[np.ndarray, np.ndarray, dict]:
    """Hold out entire flop textures. Returns train_idx, val_idx, info."""
    cards = flop_card_ids(x, tokenizer)
    uniq, inverse = np.unique(cards, axis=0, return_inverse=True)
    n_flops = int(uniq.shape[0])
    if n_flops < 2:
        raise SystemExit("need at least 2 distinct flops for a flop holdout")
    n_hold = int(holdout_flops)
    if n_hold < 1:
        raise SystemExit("--holdout-flops must be >= 1")
    if n_hold >= n_flops:
        raise SystemExit(f"--holdout-flops {n_hold} >= distinct flops {n_flops}")

    rng = np.random.default_rng(seed)
    order = rng.permutation(n_flops)
    hold_ids = order[:n_hold]
    val_mask = np.isin(inverse, hold_ids)
    val_idx = np.flatnonzero(val_mask)
    train_idx = np.flatnonzero(~val_mask)
    hold_cards = uniq[hold_ids]
    info = {
        "split": "flops",
        "seed": seed,
        "flops_total": n_flops,
        "flops_train": n_flops - n_hold,
        "flops_holdout": n_hold,
        "holdout_flops": flop_strings(hold_cards, tokenizer),
    }
    return train_idx, val_idx, info


def split_by_rows(n: int, seed: int, val_fraction: float) -> tuple[np.ndarray, np.ndarray, dict]:
    n_val = max(1, int(n * val_fraction))
    n_train = n - n_val
    rng = np.random.default_rng(seed)
    perm = rng.permutation(n)
    info = {
        "split": "rows",
        "seed": seed,
        "val_fraction": val_fraction,
    }
    return perm[:n_train], perm[n_train:], info


def write_holdout_flops(path: Path, flops: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(f"{f}\n" for f in flops))
