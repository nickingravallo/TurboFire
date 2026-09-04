#!/usr/bin/env python3
"""Generate action sequences from a NashGPT checkpoint.

Live hole+flop cards are rewritten into the 1,755-texture canonical frame
before the net runs, then card tokens in sampled lines are mapped back.
`--probs` mixes are suit-invariant and are not relabeled.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import mlx.core as mx
import numpy as np
from mlx.utils import tree_unflatten

from card_tokens import TOKEN_FORMAT_SPLIT, fuse_cards, split_cards
from model import GPT, GPTConfig
from tokenizer import Tokenizer
from flop_iso import IsoFrame, canonicalize_runout, format_perm
from legal import (
    PROFILE_DEFAULT,
    hero_legal_tokens,
    legal_mask_for_context,
    node_kind_from_context,
)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--ckpt", type=Path, default=Path("out/best"))
    p.add_argument(
        "--prompt",
        type=str,
        default="<START> <HOLE> As Ks <FLOP> Qs Jd 7c <BETTING>",
        help="Space-separated token prefix",
    )
    p.add_argument("--max-new", type=int, default=12)
    p.add_argument("--temperature", type=float, default=0.8)
    p.add_argument("--top-k", type=int, default=20)
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--n", type=int, default=5, help="number of samples")
    p.add_argument(
        "--probs",
        action="store_true",
        help="Print next-token softmax instead of sampling a continuation",
    )
    p.add_argument(
        "--actions-only",
        action="store_true",
        help="With --probs, softmax over legal HERO actions for this spot",
    )
    p.add_argument(
        "--top",
        type=int,
        default=0,
        help="With --probs, show only top-N tokens (0 = all kept tokens)",
    )
    p.add_argument(
        "--raw-prompt",
        action="store_true",
        help="Do not suit-canonicalize <HOLE>/<FLOP> before inference",
    )
    p.add_argument(
        "--keep-canonical",
        action="store_true",
        help="Print generated cards in the canonical frame (skip deconvert)",
    )
    return p.parse_args()


def load_model(ckpt: Path) -> tuple[GPT, Tokenizer, str]:
    with open(ckpt / "config.json") as f:
        config = GPTConfig.from_dict(json.load(f))
    tokenizer = Tokenizer.from_file(ckpt / "tokenizer.json")
    model = GPT(config)

    data = np.load(ckpt / "weights.npz")
    items = [(k.replace("__", "."), mx.array(data[k])) for k in data.files]
    model.update(tree_unflatten(items))
    mx.eval(model.parameters())
    model.eval()
    meta_path = ckpt / "meta.json"
    meta = json.loads(meta_path.read_text()) if meta_path.exists() else {}
    return model, tokenizer, meta.get("legal_profile", PROFILE_DEFAULT)


def sample_next(logits: mx.array, temperature: float, top_k: int) -> int:
    logits = logits.astype(mx.float32)
    if temperature <= 0:
        return int(mx.argmax(logits))

    logits = logits / temperature
    if top_k > 0:
        # Keep only the top-k logits; set the rest to -inf.
        k = min(top_k, int(logits.shape[-1]))
        top_vals = mx.partition(logits, kth=logits.shape[-1] - k)[-k:]
        threshold = mx.min(top_vals)
        logits = mx.where(logits < threshold, mx.array(-1e9, dtype=logits.dtype), logits)

    probs = mx.softmax(logits, axis=-1)
    return int(mx.random.categorical(mx.log(probs + 1e-9)))


def generate(
    model: GPT,
    tokenizer: Tokenizer,
    prompt: str,
    max_new: int,
    temperature: float,
    top_k: int,
) -> str:
    ids = tokenizer.encode(prompt)
    if not ids:
        raise ValueError("empty prompt")
    end_id = tokenizer.token_to_id.get("<END>")

    for _ in range(max_new):
        ctx = ids[-model.config.block_size :]
        logits = model(mx.array([ctx]))[0, -1]
        nxt = sample_next(logits, temperature, top_k)
        ids.append(nxt)
        if end_id is not None and nxt == end_id:
            break
    return tokenizer.decode(ids)


def next_token_probs(
    model: GPT,
    tokenizer: Tokenizer,
    prompt: str,
    actions_only: bool = False,
    legal_profile: str = PROFILE_DEFAULT,
) -> list[tuple[str, float]]:
    """Return (token, probability) pairs for the next-token softmax."""
    ids = tokenizer.encode(prompt)
    if not ids:
        raise ValueError("empty prompt")

    ctx = ids[-model.config.block_size :]
    logits = model(mx.array([ctx]))[0, -1].astype(mx.float32)
    if actions_only:
        mask = legal_mask_for_context(tokenizer, prompt, legal_profile)
        ninf = mx.array(-1e9, dtype=logits.dtype)
        logits = mx.where(mx.array(mask), logits, ninf)
    probs = np.array(mx.softmax(logits, axis=-1), dtype=np.float64)

    legal = (
        set(
            hero_legal_tokens(
                node_kind_from_context(prompt, legal_profile), legal_profile
            )
        )
        if actions_only
        else None
    )
    pairs: list[tuple[str, float]] = []
    for i, p in enumerate(probs):
        tok = tokenizer.id_to_token.get(i, "<UNK>")
        if legal is not None and tok not in legal:
            continue
        pairs.append((tok, float(p)))

    pairs.sort(key=lambda x: x[1], reverse=True)
    return pairs


def main() -> None:
    args = parse_args()
    mx.random.seed(args.seed)
    model, tokenizer, legal_profile = load_model(args.ckpt)
    frame = (
        IsoFrame(raw=args.prompt, canonical=args.prompt, perm=(0, 1, 2, 3))
        if args.raw_prompt
        else canonicalize_runout(args.prompt)
    )
    prompt = frame.canonical
    print(
        f"loaded {args.ckpt}  vocab={tokenizer.vocab_size}  "
        f"legal_profile={legal_profile} token_format={tokenizer.token_format} "
        f"params config={model.config}"
    )
    print(f"prompt: {frame.raw}")
    if frame.canonical != frame.raw:
        print(f"canonical: {frame.canonical}")
        print(f"perm: {format_perm(frame.perm)}")
    print("---")

    if args.probs:
        pairs = next_token_probs(
            model,
            tokenizer,
            prompt,
            actions_only=args.actions_only,
            legal_profile=legal_profile,
        )
        if args.top > 0:
            pairs = pairs[: args.top]
        for tok, p in pairs:
            print(f"{p:7.2%}  {tok}")
        print(f"sum={sum(p for _, p in pairs):.4f}")
        return

    for i in range(args.n):
        out = generate(
            model,
            tokenizer,
            prompt,
            max_new=args.max_new,
            temperature=args.temperature,
            top_k=args.top_k,
        )
        if not args.keep_canonical:
            out = frame.decanonicalize(fuse_cards(out))
            if tokenizer.token_format == TOKEN_FORMAT_SPLIT:
                out = split_cards(out)
        print(f"[{i+1}] {out}")


if __name__ == "__main__":
    main()
