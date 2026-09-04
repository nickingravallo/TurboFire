#!/usr/bin/env python3
"""Print the next-token action mix from a NashGPT PyTorch checkpoint.

Canonicalizes the prompt into the training suit frame. Action mixes are
not suit-relabeled; card tokens in the printed prompt are shown both raw
and canonical. Tokenizer metadata selects fused or split rank/suit encoding.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import torch
import torch.nn.functional as F

from tokenizer import Tokenizer
from flop_iso import IsoFrame, canonicalize_runout, format_perm
from legal import (
    PROFILE_DEFAULT,
    hero_legal_tokens,
    legal_mask_for_context,
    node_kind_from_context,
)
from torch_model import GPT, GPTConfig


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--ckpt", type=Path, default=Path("out_torch/best"))
    p.add_argument(
        "--prompt",
        type=str,
        default="<START> <HOLE> As Ks <FLOP> Qs Jd 7c <BETTING>",
    )
    p.add_argument(
        "--actions-only",
        action="store_true",
        help="Softmax over legal HERO actions for this spot (not the full vocab)",
    )
    p.add_argument("--top", type=int, default=0)
    p.add_argument(
        "--raw-prompt",
        action="store_true",
        help="Do not suit-canonicalize <HOLE>/<FLOP> before inference",
    )
    return p.parse_args()


def main() -> None:
    args = parse_args()
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    config = GPTConfig.from_dict(json.loads((args.ckpt / "config.json").read_text()))
    meta_path = args.ckpt / "meta.json"
    meta = json.loads(meta_path.read_text()) if meta_path.exists() else {}
    legal_profile = meta.get("legal_profile", PROFILE_DEFAULT)
    tokenizer = Tokenizer.from_file(args.ckpt / "tokenizer.json")
    model = GPT(config)
    model.load_state_dict(torch.load(args.ckpt / "weights.pt", map_location=device))
    model.to(device).eval()

    frame = (
        IsoFrame(raw=args.prompt, canonical=args.prompt, perm=(0, 1, 2, 3))
        if args.raw_prompt
        else canonicalize_runout(args.prompt)
    )
    prompt = frame.canonical
    ids = tokenizer.encode(prompt)
    if not ids:
        raise SystemExit("empty prompt")
    ids = ids[-config.block_size :]
    x = torch.tensor([ids], dtype=torch.long, device=device)
    with torch.no_grad():
        logits = model(x)[0, -1]
        if args.actions_only:
            mask = torch.from_numpy(
                legal_mask_for_context(tokenizer, prompt, legal_profile)
            ).to(
                device=device, dtype=torch.bool
            )
            logits = logits.masked_fill(~mask, torch.finfo(logits.dtype).min)
        probs = F.softmax(logits, dim=-1).cpu()

    pairs = []
    legal = (
        set(
            hero_legal_tokens(
                node_kind_from_context(prompt, legal_profile), legal_profile
            )
        )
        if args.actions_only
        else None
    )
    for i, p in enumerate(probs.tolist()):
        tok = tokenizer.id_to_token.get(i, "<UNK>")
        if legal is not None and tok not in legal:
            continue
        pairs.append((tok, p))
    pairs.sort(key=lambda x: x[1], reverse=True)
    if args.top > 0:
        pairs = pairs[: args.top]

    print(
        f"loaded {args.ckpt}  device={device}  legal_profile={legal_profile} "
        f"token_format={tokenizer.token_format}"
    )
    print(f"prompt: {frame.raw}")
    if frame.canonical != frame.raw:
        print(f"canonical: {frame.canonical}")
        print(f"perm: {format_perm(frame.perm)}")
    print("---")
    for tok, p in pairs:
        print(f"{p:7.2%}  {tok}")
    print(f"sum={sum(p for _, p in pairs):.4f}")


if __name__ == "__main__":
    main()
