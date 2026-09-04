#!/usr/bin/env python3
"""Compare NashGPT canonical and raw suit inputs with fresh TurboFire solves."""

from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import sys
import time
from pathlib import Path

import torch
import torch.nn.functional as F


DEFAULT_NASH = Path.home() / "Projects/NashGPT/NashGPT-1.5-Iso"
DEFAULT_SOLVER = Path(__file__).resolve().parents[1] / "turbofire"

CASES = (
    ("broadway-rainbow", "Ah Kh", "Qh Jd 7c"),
    ("connected-two-tone", "As Qd", "9h 8h 6d"),
    ("ace-high-monotone", "Kh Qd", "As 7s 2s"),
    ("paired-dry", "Ah Qh", "Kh Kd 4c"),
    ("trips", "Ah Kh", "7h 7d 7c"),
    ("broadway-connected", "Js Ts", "Ac Kd Qh"),
    ("disconnected-two-tone", "Ah Kd", "Jh 5h 2c"),
    ("low-connected-rainbow", "As Ks", "6c 5d 4h"),
    ("ace-high-dry", "Kh Qh", "Ah 9d 2c"),
    ("paired-connected", "As Ks", "Qc Qh Js"),
    ("low-two-tone", "Ah Kh", "8s 3s 2d"),
    ("connected-monotone", "Ah Kd", "Tc 9c 8c"),
)
PATHS = (
    ("", "oop-open"),
    (" OPP_CHECK", "ip-after-check"),
    (" OPP_BET100", "ip-vs-bet100"),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nash", type=Path, default=DEFAULT_NASH)
    parser.add_argument("--solver", type=Path, default=DEFAULT_SOLVER)
    parser.add_argument("--iterations", type=int, default=1000)
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def distribution_metrics(pred: dict[str, float], target: dict[str, float]) -> dict[str, float]:
    actions = sorted(set(pred) | set(target))
    p = [pred.get(action, 0.0) for action in actions]
    q = [target.get(action, 0.0) for action in actions]
    tvd = 0.5 * sum(abs(a - b) for a, b in zip(p, q))
    brier = sum((a - b) ** 2 for a, b in zip(p, q))
    midpoint = [(a + b) / 2.0 for a, b in zip(p, q)]

    def kl_bits(left: list[float], right: list[float]) -> float:
        return sum(
            a * math.log2(a / b)
            for a, b in zip(left, right)
            if a > 0.0 and b > 0.0
        )

    js_bits = 0.5 * kl_bits(p, midpoint) + 0.5 * kl_bits(q, midpoint)
    return {"tvd": tvd, "brier": brier, "js_bits": js_bits}


def mean(values: list[float]) -> float:
    return sum(values) / len(values)


def main() -> None:
    args = parse_args()
    sys.path.insert(0, str(args.nash))
    from flop_iso import canonicalize_runout  # type: ignore
    from legal import hero_legal_tokens, legal_mask_for_context, node_kind_from_context  # type: ignore
    from tokenizer import Tokenizer  # type: ignore
    from torch_model import GPT, GPTConfig  # type: ignore

    config = GPTConfig.from_dict(json.loads((args.nash / "config.json").read_text()))
    tokenizer = Tokenizer.from_file(args.nash / "tokenizer.json")
    model = GPT(config)
    model.load_state_dict(
        torch.load(args.nash / "weights.pt", map_location="cpu", weights_only=True)
    )
    model.eval()

    def infer(prompt: str) -> dict[str, float]:
        ids = tokenizer.encode(prompt)[-config.block_size :]
        x = torch.tensor([ids], dtype=torch.long)
        with torch.no_grad():
            logits = model(x)[0, -1]
            mask = torch.from_numpy(legal_mask_for_context(tokenizer, prompt)).bool()
            probs = F.softmax(logits.masked_fill(~mask, torch.finfo(logits.dtype).min), -1)
        return {
            action: float(probs[tokenizer.token_to_id[action]])
            for action in hero_legal_tokens(node_kind_from_context(prompt))
        }

    prompts: list[dict[str, str]] = []
    boards: dict[str, str] = {}
    for texture, hole, flop in CASES:
        for path, node in PATHS:
            raw = f"<START> <HOLE> {hole} <FLOP> {flop} <BETTING>{path}"
            canonical = canonicalize_runout(raw).canonical
            tokens = canonical.split()
            flop_start = tokens.index("<FLOP>") + 1
            board = "".join(tokens[flop_start : flop_start + 3])
            boards.setdefault(board, texture)
            prompts.append(
                {
                    "texture": texture,
                    "node": node,
                    "raw_prompt": raw,
                    "canonical_prompt": canonical,
                    "board": board,
                }
            )

    env = os.environ.copy()
    env.update(
        SOFT_MAX_DEPTH="1",
        SOFT_MAX_COMBOS="0",
        SEED="42",
        OMP_NUM_THREADS=str(args.threads),
    )
    solver_rows: dict[str, dict[str, float]] = {}
    solve_times: dict[str, float] = {}
    started = time.monotonic()
    for board in boards:
        solve_started = time.monotonic()
        completed = subprocess.run(
            [str(args.solver), board, str(args.iterations), "--range=wide"],
            env=env,
            text=True,
            capture_output=True,
            check=True,
        )
        solve_times[board] = time.monotonic() - solve_started
        for line in completed.stdout.splitlines():
            if not line.startswith('{"context"'):
                continue
            row = json.loads(line)
            solver_rows[row["context"]] = row["action_probs"]

    results: list[dict[str, object]] = []
    missing: list[str] = []
    for spot in prompts:
        canonical_prompt = spot["canonical_prompt"]
        gto = solver_rows.get(canonical_prompt)
        if gto is None:
            missing.append(canonical_prompt)
            continue
        canonical_pred = infer(canonical_prompt)
        raw_pred = infer(spot["raw_prompt"])
        results.append(
            {
                **spot,
                "gto": gto,
                "model_canonical": canonical_pred,
                "model_raw_noncanonical": raw_pred,
                "canonical_vs_gto": distribution_metrics(canonical_pred, gto),
                "raw_vs_gto": distribution_metrics(raw_pred, gto),
                "canonical_vs_raw": distribution_metrics(canonical_pred, raw_pred),
                "top_gto": max(gto, key=gto.get),
                "top_canonical": max(canonical_pred, key=canonical_pred.get),
                "top_raw": max(raw_pred, key=raw_pred.get),
            }
        )

    if missing:
        raise RuntimeError(f"{len(missing)} benchmark prompts absent from solver output")

    def aggregate(metric_group: str, metric: str, subset: list[dict[str, object]]) -> float:
        return mean(
            [float(result[metric_group][metric]) for result in subset]  # type: ignore[index]
        )

    def summarize(subset: list[dict[str, object]]) -> dict[str, object]:
        return {
            "spots": len(subset),
            "canonical": {
                metric: aggregate("canonical_vs_gto", metric, subset)
                for metric in ("tvd", "brier", "js_bits")
            },
            "raw_noncanonical": {
                metric: aggregate("raw_vs_gto", metric, subset)
                for metric in ("tvd", "brier", "js_bits")
            },
            "canonical_raw_disagreement": {
                metric: aggregate("canonical_vs_raw", metric, subset)
                for metric in ("tvd", "brier", "js_bits")
            },
            "top_action_accuracy": {
                "canonical": mean(
                    [result["top_canonical"] == result["top_gto"] for result in subset]
                ),
                "raw_noncanonical": mean(
                    [result["top_raw"] == result["top_gto"] for result in subset]
                ),
            },
        }

    summary = {
        **summarize(results),
        "textures": len(CASES),
        "iterations_per_flop": args.iterations,
        "by_node": {
            node: summarize([result for result in results if result["node"] == node])
            for _, node in PATHS
        },
        "elapsed_seconds": time.monotonic() - started,
        "solve_seconds_by_board": solve_times,
    }
    payload = {
        "method": {
            "model": str(args.nash),
            "solver": str(args.solver),
            "range": "wide",
            "seed": 42,
            "conditions": {
                "canonical": "Suit-canonicalized prompt passed to the model",
                "raw_noncanonical": "Original physical suits passed with canonicalization bypassed",
                "gto": "Fresh TurboFire DCFR average strategy",
            },
        },
        "summary": summary,
        "results": results,
    }
    rendered = json.dumps(payload, indent=2, sort_keys=True)
    if args.output:
        args.output.write_text(rendered + "\n")
    print(rendered)


if __name__ == "__main__":
    main()
