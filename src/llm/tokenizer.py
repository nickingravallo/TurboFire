"""Whitespace tokenizer over TurboFire action / soft-label tokens.

Hard lines:
  <START> <HOLE> As Ks <FLOP> Qs Jd 7c <BETTING> HERO_BET100 OPP_CALL <SHOWDOWN> <END>

Soft JSONL contexts use the same token strings; action mixes are not tokenized.
Actions are egocentric: HERO_ is the hole-card holder, OPP_ is the other seat.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Iterable

from card_tokens import (
    RANKS,
    SUITS,
    TOKEN_FORMAT_FUSED,
    TOKEN_FORMAT_SPLIT,
    TOKEN_FORMATS,
    normalize_context,
)

PAD = "<PAD>"
UNK = "<UNK>"
HERO_PREFIX = "HERO_"
OPP_PREFIX = "OPP_"

# Seeded so soft-label training always has the full action set even if a file
# is missing some actions.
SEED_SPECIALS = [
    "<START>",
    "<END>",
    "<HOLE>",
    "<FLOP>",
    "<BETTING>",
    "<TURN>",
    "<RIVER>",
    "<SHOWDOWN>",
    "<FOLD_END>",
]
SEED_ACTIONS = [
    "FOLD",
    "CALL",
    "CHECK",
    "BET10",
    "BET25",
    "BET40",
    "BET52",
    "BET100",
    "BET123",
    "RAISE_2X",
    "RAISE_3X",
    "RAISE_4X",
    "ALLIN",
]


def seed_tokens(token_format: str = TOKEN_FORMAT_FUSED) -> set[str]:
    if token_format not in TOKEN_FORMATS:
        raise ValueError(f"unknown token format: {token_format}")
    tokens = {PAD, UNK, *SEED_SPECIALS}
    if token_format == TOKEN_FORMAT_FUSED:
        for rank in RANKS:
            for suit in SUITS:
                tokens.add(f"{rank}{suit}")
    else:
        tokens.update(RANKS)
        tokens.update(SUITS)
    for prefix in (HERO_PREFIX, OPP_PREFIX):
        for action in SEED_ACTIONS:
            tokens.add(prefix + action)
    return tokens


class Tokenizer:
    def __init__(
        self,
        token_to_id: dict[str, int],
        token_format: str = TOKEN_FORMAT_FUSED,
    ):
        if token_format not in TOKEN_FORMATS:
            raise ValueError(f"unknown token format: {token_format}")
        self.token_to_id = dict(token_to_id)
        self.token_format = token_format
        self.id_to_token = {i: t for t, i in self.token_to_id.items()}
        if PAD not in self.token_to_id:
            raise ValueError(f"vocab missing {PAD}")
        if UNK not in self.token_to_id:
            raise ValueError(f"vocab missing {UNK}")
        self.pad_id = self.token_to_id[PAD]
        self.unk_id = self.token_to_id[UNK]

    @property
    def vocab_size(self) -> int:
        return len(self.token_to_id)

    @classmethod
    def _from_token_set(
        cls, tokens: set[str], token_format: str = TOKEN_FORMAT_FUSED
    ) -> "Tokenizer":
        specials = [PAD, UNK]
        rest = sorted(t for t in tokens if t not in specials)
        token_to_id = {t: i for i, t in enumerate(specials + rest)}
        return cls(token_to_id, token_format)

    @classmethod
    def build(
        cls,
        lines: Iterable[str],
        token_format: str = TOKEN_FORMAT_FUSED,
    ) -> "Tokenizer":
        tokens = seed_tokens(token_format)
        for line in lines:
            tokens.update(normalize_context(line, token_format).split())
        return cls._from_token_set(tokens, token_format)

    @classmethod
    def build_from_soft(
        cls,
        records: Iterable[dict],
        token_format: str = TOKEN_FORMAT_FUSED,
    ) -> "Tokenizer":
        tokens = seed_tokens(token_format)
        for rec in records:
            context = normalize_context(str(rec.get("context", "")), token_format)
            tokens.update(context.split())
            probs = rec.get("action_probs") or {}
            tokens.update(probs.keys())
        return cls._from_token_set(tokens, token_format)

    @classmethod
    def from_file(cls, path: str | Path) -> "Tokenizer":
        with open(path) as f:
            payload = json.load(f)
        return cls(
            payload["token_to_id"],
            payload.get("token_format", TOKEN_FORMAT_FUSED),
        )

    def save(self, path: str | Path) -> None:
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w") as f:
            json.dump(
                {
                    "token_format": self.token_format,
                    "token_to_id": self.token_to_id,
                },
                f,
                indent=2,
                sort_keys=True,
            )

    def encode(self, text: str) -> list[int]:
        normalized = normalize_context(text, self.token_format)
        return [
            self.token_to_id.get(t, self.unk_id)
            for t in normalized.split()
            if t
        ]

    def decode(self, ids: Iterable[int]) -> str:
        return " ".join(self.id_to_token.get(int(i), UNK) for i in ids if int(i) != self.pad_id)
