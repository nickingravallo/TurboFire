#!/usr/bin/env python3
"""Fresh five-spot NashGPT 1.6 comparison for the project write-up."""

from __future__ import annotations

import json
import math
import os
import subprocess
import time
from pathlib import Path

import torch
import torch.nn.functional as F

from flop_iso import apply_suit_perm, canonicalize_runout
from legal import hero_legal_tokens, legal_mask_for_context, node_kind_from_context
from tokenizer import Tokenizer
from torch_model import GPT, GPTConfig


ROOT = Path(__file__).resolve().parents[1]
CKPT = Path(__file__).resolve().parent / "out_torch_16" / "best"
SOLVER = ROOT / "turbofire"
OUTPUT = Path(__file__).resolve().parent / "nashgpt16_five_spots.json"

CASES = (
    ("Broadway rainbow", "Ah Kh", "Qh Jd 7c", ""),
    ("Connected two-tone", "As Qd", "9h 8h 6d", " OPP_CHECK"),
    ("Ace-high monotone", "Kh Qd", "As 7s 2s", " HERO_CHECK OPP_BET40"),
    (
        "Broadway connected",
        "Js Ts",
        "Ac Kd Qh",
        " OPP_CHECK HERO_BET40 OPP_RAISE_3X",
    ),
    ("Paired dry", "Ah Qh", "Kh Kd 4c", " HERO_CHECK OPP_ALLIN"),
)


def metrics(pred: dict[str, float], target: dict[str, float]) -> dict[str, float]:
    actions = sorted(set(pred) | set(target))
    tvd = 0.5 * sum(abs(pred.get(a, 0.0) - target.get(a, 0.0)) for a in actions)
    return {"tvd": tvd}


def main() -> None:
    config = GPTConfig.from_dict(json.loads((CKPT / "config.json").read_text()))
    meta = json.loads((CKPT / "meta.json").read_text())
    profile = meta["legal_profile"]
    tokenizer = Tokenizer.from_file(CKPT / "tokenizer.json")
    model = GPT(config)
    model.load_state_dict(
        torch.load(CKPT / "weights.pt", map_location="cpu", weights_only=True)
    )
    model.eval()

    def infer(prompt: str) -> dict[str, float]:
        ids = tokenizer.encode(prompt)[-config.block_size :]
        x = torch.tensor([ids], dtype=torch.long)
        with torch.inference_mode():
            logits = model(x)[0, -1]
            mask = torch.from_numpy(
                legal_mask_for_context(tokenizer, prompt, profile)
            ).bool()
            probs = F.softmax(
                logits.masked_fill(~mask, torch.finfo(logits.dtype).min), dim=-1
            )
        return {
            action: float(probs[tokenizer.token_to_id[action]])
            for action in hero_legal_tokens(
                node_kind_from_context(prompt, profile), profile
            )
        }

    env = os.environ.copy()
    env.update(
        SOFT_MAX_DEPTH="3",
        SOFT_MAX_COMBOS="0",
        BET_SIZES="40,100",
        RAISE_SIZES="3",
        MAX_RAISES="1",
        ENABLE_ALLIN="1",
        SEED="42",
        OMP_NUM_THREADS="8",
    )
    results = []
    started = time.monotonic()
    for name, hole, flop, history in CASES:
        requested = f"<START> <HOLE> {hole} <FLOP> {flop} <BETTING>{history}"
        requested_canonical = canonicalize_runout(requested).canonical
        tokens = requested_canonical.split()
        flop_start = tokens.index("<FLOP>") + 1
        board = "".join(tokens[flop_start : flop_start + 3])
        solve_started = time.monotonic()
        completed = subprocess.run(
            [
                str(SOLVER),
                board,
                "1000",
                "--range=condensed",
                "--bets=40,100",
                "--raises=3",
                "--max-raises=1",
                "--allin",
            ],
            env=env,
            text=True,
            capture_output=True,
            check=True,
        )
        rows = {}
        for line in completed.stdout.splitlines():
            if line.startswith('{"context"'):
                row = json.loads(line)
                rows[row["context"]] = row["action_probs"]
        source = requested_canonical
        target = rows.get(source)
        if target is None:
            suffix = f"<BETTING>{history}"
            candidates = sorted(context for context in rows if context.endswith(suffix))
            if not candidates:
                wanted_depth = len(history.split())
                candidates = sorted(
                    context
                    for context in rows
                    if len(context.split()) - context.split().index("<BETTING>") - 1
                    == wanted_depth
                )
            if not candidates:
                raise RuntimeError(f"solver did not emit any spot at depth {len(history.split())}")
            source = candidates[len(candidates) // 2]
            target = rows[source]
        canonical = canonicalize_runout(source).canonical
        raw = apply_suit_perm(source, (1, 2, 3, 0))
        canonical_pred = infer(canonical)
        raw_pred = infer(raw)
        results.append(
            {
                "name": name,
                "depth": len(history.split()),
                "raw_prompt": raw,
                "canonical_prompt": canonical,
                "solver_dcfr": target,
                "model_canonical": canonical_pred,
                "model_raw": raw_pred,
                "canonical_tvd": metrics(canonical_pred, target)["tvd"],
                "raw_tvd": metrics(raw_pred, target)["tvd"],
                "solve_seconds": time.monotonic() - solve_started,
            }
        )

    payload = {
        "method": {
            "iterations": 1000,
            "range": "condensed",
            "legal_profile": profile,
            "note": (
                "Fresh TurboFire DCFR average strategies are solver references, "
                "not independently proven true GTO."
            ),
        },
        "results": results,
        "elapsed_seconds": time.monotonic() - started,
    }
    OUTPUT.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    print(json.dumps(payload, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
