#!/usr/bin/env python3
"""Evaluate a NashGPT checkpoint on complete held-out flop textures.

The reference distributions are the matching saved solver labels. Two model
conditions are evaluated:
  canonical: the context exactly as represented in the training suit frame;
  raw:       a deterministic non-identity suit relabeling, without rewriting.
"""

from __future__ import annotations

import argparse
import heapq
import json
import math
import time
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

import numpy as np
import torch
import torch.nn.functional as F

from card_tokens import section_cards
from flop_iso import apply_suit_perm, canonicalize_runout
from legal import (
    KIND_FACE_BET,
    KIND_FACE_CAP,
    KIND_OPEN,
    PROFILE_DEFAULT,
    kind_masks,
    node_kind_from_context,
)
from tokenizer import Tokenizer
from torch_model import GPT, GPTConfig


KIND_NAMES = {
    KIND_OPEN: "open",
    KIND_FACE_BET: "facing_bet",
    KIND_FACE_CAP: "facing_cap",
}
# Cyclic relabeling makes every card token noncanonical while preserving the spot.
RAW_SUIT_PERM = (1, 2, 3, 0)
RANK_VALUE = {rank: i for i, rank in enumerate("23456789TJQKA")}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ckpt", type=Path, default=Path("out_torch_16/best"))
    parser.add_argument(
        "--data",
        type=Path,
        default=Path("../training_soft_nashgpt16_condensed.jsonl"),
    )
    parser.add_argument(
        "--holdout-flops", type=Path, default=Path("out_torch_16/holdout_flops.txt")
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--device", choices=("auto", "cpu", "cuda", "mps"), default="auto")
    parser.add_argument("--worst", type=int, default=50)
    parser.add_argument(
        "--max-rows",
        type=int,
        default=0,
        help="Testing only; zero scans every row on every held-out flop",
    )
    return parser.parse_args()


def flop_key(context: str) -> str:
    return " ".join(section_cards(context, "<FLOP>", 3))


def history_depth(context: str) -> int:
    tokens = context.split()
    return len(tokens) - tokens.index("<BETTING>") - 1


def board_texture(context: str) -> str:
    flop = section_cards(context, "<FLOP>", 3)
    ranks = [RANK_VALUE[c[0]] for c in flop]
    suits = [c[1] for c in flop]
    rank_counts = sorted((ranks.count(r) for r in set(ranks)), reverse=True)
    if rank_counts[0] == 3:
        pairing = "trips"
    elif rank_counts[0] == 2:
        pairing = "paired"
    else:
        pairing = "unpaired"
    suit_count = len(set(suits))
    suitedness = {1: "monotone", 2: "two_tone", 3: "rainbow"}[suit_count]
    unique = sorted(set(ranks))
    connected = len(unique) == 3 and max(unique) - min(unique) <= 4
    return f"{pairing}_{suitedness}_{'connected' if connected else 'disconnected'}"


def hole_board_class(context: str) -> str:
    hole = section_cards(context, "<HOLE>", 2)
    flop = section_cards(context, "<FLOP>", 3)
    hr = [RANK_VALUE[c[0]] for c in hole]
    br = [RANK_VALUE[c[0]] for c in flop]
    all_counts = {r: hr.count(r) + br.count(r) for r in set(hr + br)}
    if max(all_counts.values()) >= 4:
        return "quads"
    triples = sum(v == 3 for v in all_counts.values())
    pairs = sum(v == 2 for v in all_counts.values())
    if triples and pairs:
        return "full_house"
    if triples:
        return "trips_or_set"
    if pairs >= 2:
        return "two_pair"
    if hr[0] == hr[1]:
        if hr[0] > max(br):
            return "overpair"
        if hr[0] < min(br):
            return "underpair"
        return "pocket_pair_between"
    matched = sorted({r for r in hr if r in br}, reverse=True)
    if matched:
        distinct_board = sorted(set(br), reverse=True)
        index = distinct_board.index(matched[0])
        return ("top_pair", "middle_pair", "bottom_pair")[min(index, 2)]
    if min(hr) > max(br):
        return "two_overcards"
    if max(hr) > max(br):
        return "one_overcard"
    return "unpaired_no_overcard"


def choose_device(name: str) -> torch.device:
    if name != "auto":
        return torch.device(name)
    if torch.cuda.is_available():
        return torch.device("cuda")
    if torch.backends.mps.is_available():
        return torch.device("mps")
    return torch.device("cpu")


@dataclass
class MetricStore:
    tvd: list[float] = field(default_factory=list)
    ce: list[float] = field(default_factory=list)
    brier: list[float] = field(default_factory=list)
    js_bits: list[float] = field(default_factory=list)
    top_correct: int = 0
    count: int = 0

    def add(
        self,
        tvd: np.ndarray,
        ce: np.ndarray,
        brier: np.ndarray,
        js_bits: np.ndarray,
        top_correct: np.ndarray,
    ) -> None:
        self.tvd.extend(tvd.tolist())
        self.ce.extend(ce.tolist())
        self.brier.extend(brier.tolist())
        self.js_bits.extend(js_bits.tolist())
        self.top_correct += int(top_correct.sum())
        self.count += len(tvd)

    def summary(self) -> dict[str, object]:
        tvd = np.asarray(self.tvd, dtype=np.float64)
        return {
            "rows": self.count,
            "tvd": {
                "mean": float(tvd.mean()),
                "p50": float(np.quantile(tvd, 0.50)),
                "p90": float(np.quantile(tvd, 0.90)),
                "p95": float(np.quantile(tvd, 0.95)),
                "p99": float(np.quantile(tvd, 0.99)),
                "max": float(tvd.max()),
            },
            "soft_cross_entropy_mean": float(np.mean(self.ce)),
            "brier_mean": float(np.mean(self.brier)),
            "js_bits_mean": float(np.mean(self.js_bits)),
            "top_action_accuracy": self.top_correct / self.count,
        }


def summarize_grouped(
    grouped: dict[str, dict[str, MetricStore]]
) -> dict[str, dict[str, object]]:
    return {
        key: {condition: store.summary() for condition, store in conditions.items()}
        for key, conditions in sorted(grouped.items())
    }


def batched(iterable: Iterable[dict], size: int) -> Iterable[list[dict]]:
    batch: list[dict] = []
    for item in iterable:
        batch.append(item)
        if len(batch) == size:
            yield batch
            batch = []
    if batch:
        yield batch


def read_holdout_rows(
    data: Path, holdout: set[str], max_rows: int
) -> Iterable[dict]:
    emitted = 0
    with data.open() as stream:
        for line_number, line in enumerate(stream, 1):
            row = json.loads(line)
            context = row.get("context", "")
            if flop_key(context) not in holdout:
                continue
            row["_line"] = line_number
            yield row
            emitted += 1
            if max_rows and emitted >= max_rows:
                return


def encode_batch(
    contexts: list[str], tokenizer: Tokenizer, block_size: int, device: torch.device
) -> tuple[torch.Tensor, torch.Tensor]:
    encoded = [tokenizer.encode(context)[-block_size:] for context in contexts]
    width = max(len(ids) for ids in encoded)
    x = torch.full(
        (len(encoded), width), tokenizer.pad_id, dtype=torch.long, device=device
    )
    last = torch.empty(len(encoded), dtype=torch.long, device=device)
    for i, ids in enumerate(encoded):
        x[i, : len(ids)] = torch.tensor(ids, dtype=torch.long, device=device)
        last[i] = len(ids) - 1
    return x, last


def infer(
    model: GPT,
    contexts: list[str],
    kinds: np.ndarray,
    masks: torch.Tensor,
    tokenizer: Tokenizer,
    device: torch.device,
) -> np.ndarray:
    x, last = encode_batch(contexts, tokenizer, model.config.block_size, device)
    logits = model(x)[torch.arange(len(contexts), device=device), last]
    legal = masks[torch.from_numpy(kinds).to(device=device, dtype=torch.long)]
    probs = F.softmax(
        logits.masked_fill(~legal, torch.finfo(logits.dtype).min), dim=-1
    )
    return probs.float().cpu().numpy()


def metric_arrays(
    pred: np.ndarray, target: np.ndarray
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    eps = 1e-12
    tvd = 0.5 * np.abs(pred - target).sum(axis=1)
    ce = -(target * np.log(np.clip(pred, eps, 1.0))).sum(axis=1)
    brier = np.square(pred - target).sum(axis=1)
    midpoint = 0.5 * (pred + target)
    pred_kl = (pred * np.log2(np.clip(pred / np.clip(midpoint, eps, None), eps, None))).sum(axis=1)
    target_kl = (
        target * np.log2(np.clip(target / np.clip(midpoint, eps, None), eps, None))
    ).sum(axis=1)
    js_bits = 0.5 * (pred_kl + target_kl)
    top_correct = pred.argmax(axis=1) == target.argmax(axis=1)
    return tvd, ce, brier, js_bits, top_correct


def main() -> None:
    args = parse_args()
    started = time.monotonic()
    device = choose_device(args.device)
    meta = json.loads((args.ckpt / "meta.json").read_text())
    legal_profile = meta.get("legal_profile", PROFILE_DEFAULT)
    config = GPTConfig.from_dict(json.loads((args.ckpt / "config.json").read_text()))
    tokenizer = Tokenizer.from_file(args.ckpt / "tokenizer.json")
    model = GPT(config)
    model.load_state_dict(
        torch.load(args.ckpt / "weights.pt", map_location=device, weights_only=True)
    )
    model.to(device).eval()
    masks_np = kind_masks(tokenizer, legal_profile)
    masks = torch.from_numpy(masks_np).to(device=device, dtype=torch.bool)
    holdout = {
        line.strip() for line in args.holdout_flops.read_text().splitlines() if line.strip()
    }

    overall = {condition: MetricStore() for condition in ("canonical", "raw")}
    grouped_names = ("depth", "actor", "node_kind", "texture", "hole_board_class")
    grouped: dict[str, dict[str, dict[str, MetricStore]]] = {
        name: defaultdict(lambda: {c: MetricStore() for c in ("canonical", "raw")})
        for name in grouped_names
    }
    worst: dict[str, list[tuple[float, int, dict]]] = {"canonical": [], "raw": []}
    rows_seen = 0
    canonical_equivalence_failures = 0

    print(
        f"device={device} profile={legal_profile} holdout_flops={len(holdout)} "
        f"batch_size={args.batch_size}",
        flush=True,
    )
    with torch.inference_mode():
        rows = read_holdout_rows(args.data, holdout, args.max_rows)
        for batch_index, batch in enumerate(batched(rows, args.batch_size), 1):
            source_contexts = [row["context"] for row in batch]
            canonical_contexts = [
                canonicalize_runout(context).canonical for context in source_contexts
            ]
            raw_contexts = [
                apply_suit_perm(context, RAW_SUIT_PERM) for context in source_contexts
            ]
            canonical_equivalence_failures += sum(
                canonicalize_runout(raw).canonical != canonical
                for raw, canonical in zip(raw_contexts, canonical_contexts)
            )
            kinds = np.asarray(
                [node_kind_from_context(context, legal_profile) for context in canonical_contexts],
                dtype=np.int64,
            )
            target = np.zeros((len(batch), tokenizer.vocab_size), dtype=np.float32)
            for i, row in enumerate(batch):
                for action, probability in row["action_probs"].items():
                    target[i, tokenizer.token_to_id[action]] = float(probability)
            target *= masks_np[kinds]
            target /= np.clip(target.sum(axis=1, keepdims=True), 1e-12, None)

            predictions = {
                "canonical": infer(
                    model, canonical_contexts, kinds, masks, tokenizer, device
                ),
                "raw": infer(model, raw_contexts, kinds, masks, tokenizer, device),
            }
            depths = [history_depth(context) for context in source_contexts]
            dimensions = {
                "depth": [str(depth) for depth in depths],
                "actor": ["OOP" if depth % 2 == 0 else "IP" for depth in depths],
                "node_kind": [KIND_NAMES[int(kind)] for kind in kinds],
                "texture": [board_texture(context) for context in source_contexts],
                "hole_board_class": [
                    hole_board_class(context) for context in source_contexts
                ],
            }

            for condition, pred in predictions.items():
                arrays = metric_arrays(pred, target)
                overall[condition].add(*arrays)
                for dimension, values in dimensions.items():
                    for value in set(values):
                        indices = np.asarray(
                            [i for i, candidate in enumerate(values) if candidate == value]
                        )
                        grouped[dimension][value][condition].add(
                            *(array[indices] for array in arrays)
                        )
                tvd = arrays[0]
                for i in range(len(batch)):
                    action_ids = np.flatnonzero(masks_np[kinds[i]])
                    detail = {
                        "dataset_line": batch[i]["_line"],
                        "source_context": source_contexts[i],
                        "canonical_context": canonical_contexts[i],
                        "raw_context": raw_contexts[i],
                        "tvd": float(tvd[i]),
                        "target": {
                            tokenizer.id_to_token[int(j)]: float(target[i, j])
                            for j in action_ids
                        },
                        "prediction": {
                            tokenizer.id_to_token[int(j)]: float(pred[i, j])
                            for j in action_ids
                        },
                    }
                    entry = (float(tvd[i]), int(batch[i]["_line"]), detail)
                    if len(worst[condition]) < args.worst:
                        heapq.heappush(worst[condition], entry)
                    elif entry[:2] > worst[condition][0][:2]:
                        heapq.heapreplace(worst[condition], entry)

            rows_seen += len(batch)
            if batch_index % 100 == 0:
                elapsed = time.monotonic() - started
                print(
                    f"rows={rows_seen:,} rate={rows_seen / elapsed:,.0f}/s "
                    f"elapsed={elapsed / 60:.1f}m",
                    flush=True,
                )

    if canonical_equivalence_failures:
        raise RuntimeError(
            f"{canonical_equivalence_failures} raw prompts were not strategically "
            "equivalent under canonicalization"
        )
    expected_rows = int(meta.get("val_lines", 0))
    if not args.max_rows and rows_seen != expected_rows:
        raise RuntimeError(f"evaluated {rows_seen:,} rows; checkpoint expects {expected_rows:,}")

    payload = {
        "method": {
            "reference": "saved TurboFire DCFR average-strategy labels",
            "gto_claim": (
                "This measures model-to-solver fidelity. It does not independently "
                "establish solver convergence, exploitability, or true GTO distance."
            ),
            "checkpoint": str(args.ckpt.resolve()),
            "checkpoint_step": meta.get("step"),
            "checkpoint_val_loss": meta.get("val_loss"),
            "dataset": str(args.data.resolve()),
            "holdout_file": str(args.holdout_flops.resolve()),
            "holdout_flops": len(holdout),
            "rows": rows_seen,
            "legal_profile": legal_profile,
            "token_format": tokenizer.token_format,
            "requires_canonicalization": meta.get("requires_canonicalization", True),
            "device": str(device),
            "raw_suit_permutation": list(RAW_SUIT_PERM),
            "conditions": {
                "canonical": (
                    "Dataset context passed through the production hole+flop "
                    "canonicalizer before inference"
                ),
                "raw": (
                    "Same spot under a cyclic noncanonical suit relabeling, passed "
                    "directly without canonicalization"
                ),
            },
        },
        "summary": {condition: store.summary() for condition, store in overall.items()},
        "breakdowns": {
            dimension: summarize_grouped(values)
            for dimension, values in grouped.items()
        },
        "worst_examples": {
            condition: [
                detail
                for _score, _line, detail in sorted(entries, reverse=True)
            ]
            for condition, entries in worst.items()
        },
        "elapsed_seconds": time.monotonic() - started,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    print(json.dumps(payload["summary"], indent=2, sort_keys=True))
    print(f"report={args.output} elapsed={payload['elapsed_seconds'] / 60:.1f}m")


if __name__ == "__main__":
    main()
