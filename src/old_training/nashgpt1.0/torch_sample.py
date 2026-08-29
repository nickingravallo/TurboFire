#!/usr/bin/env python3
"""Print next-token action mix from a PyTorch soft-label checkpoint."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import torch
import torch.nn.functional as F

from tokenizer import HERO_PREFIX, Tokenizer
from flop_iso import canonicalize_prompt
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
        help="Keep HERO_ actions only and renormalize",
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
    tokenizer = Tokenizer.from_file(args.ckpt / "tokenizer.json")
    model = GPT(config)
    model.load_state_dict(torch.load(args.ckpt / "weights.pt", map_location=device))
    model.to(device).eval()

    prompt = args.prompt if args.raw_prompt else canonicalize_prompt(args.prompt)
    ids = tokenizer.encode(prompt)
    if not ids:
        raise SystemExit("empty prompt")
    ids = ids[-config.block_size :]
    x = torch.tensor([ids], dtype=torch.long, device=device)
    with torch.no_grad():
        logits = model(x)[0, -1]
        probs = F.softmax(logits, dim=-1).cpu()

    pairs = []
    for i, p in enumerate(probs.tolist()):
        tok = tokenizer.id_to_token.get(i, "<UNK>")
        if args.actions_only and not tok.startswith(HERO_PREFIX):
            continue
        pairs.append((tok, p))
    if args.actions_only:
        total = sum(p for _, p in pairs) or 1.0
        pairs = [(t, p / total) for t, p in pairs]
    pairs.sort(key=lambda x: x[1], reverse=True)
    if args.top > 0:
        pairs = pairs[: args.top]

    print(f"loaded {args.ckpt}  device={device}")
    print(f"prompt: {prompt}")
    if prompt != args.prompt:
        print(f"raw prompt: {args.prompt}")
    print("---")
    for tok, p in pairs:
        print(f"{p:7.2%}  {tok}")
    print(f"sum={sum(p for _, p in pairs):.4f}")


if __name__ == "__main__":
    main()
