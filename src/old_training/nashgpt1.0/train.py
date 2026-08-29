#!/usr/bin/env python3
"""Train a tiny poker GPT on TurboFire data using MLX (Apple Silicon).

Hard next-token lines (.txt):
  python train.py --data ../trainingdata.txt --out out

Soft-label policy mixes (.jsonl):
  python train.py --data ../training_soft.jsonl --out out_soft

Then inspect mixes:
  python sample.py --ckpt out_soft/best --probs --actions-only \\
    --prompt "<START> <HOLE> As Ks <FLOP> Qs Jd 7c <BETTING>"
"""

from __future__ import annotations

import argparse
import json
import math
import time
from pathlib import Path

import mlx.core as mx
import mlx.nn as nn
import mlx.optimizers as optim
import numpy as np
from mlx.utils import tree_flatten

from model import GPT, GPTConfig
from tokenizer import PAD, Tokenizer


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Train tiny TurboFire GPT with MLX")
    p.add_argument("--data", type=Path, default=Path("../training_soft.jsonl"))
    p.add_argument("--out", type=Path, default=Path("out_soft"))
    p.add_argument(
        "--mode",
        choices=("auto", "hard", "soft"),
        default="auto",
        help="auto: .jsonl => soft, else hard",
    )
    p.add_argument("--block-size", type=int, default=32)
    p.add_argument("--n-layer", type=int, default=6)
    p.add_argument("--n-head", type=int, default=6)
    p.add_argument("--n-embd", type=int, default=384)
    p.add_argument("--dropout", type=float, default=0.0)
    p.add_argument("--batch-size", type=int, default=256)
    p.add_argument("--epochs", type=int, default=3)
    p.add_argument("--lr", type=float, default=3e-4)
    p.add_argument("--weight-decay", type=float, default=0.1)
    p.add_argument("--warmup-steps", type=int, default=200)
    p.add_argument("--val-fraction", type=float, default=0.02)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--log-every", type=int, default=50)
    p.add_argument("--eval-every", type=int, default=500)
    p.add_argument("--max-batches", type=int, default=0, help="0 = full epoch")
    return p.parse_args()


def resolve_mode(args: argparse.Namespace) -> str:
    if args.mode != "auto":
        return args.mode
    return "soft" if args.data.suffix.lower() == ".jsonl" else "hard"


def load_lines(path: Path) -> list[str]:
    with open(path) as f:
        lines = [ln.strip() for ln in f if ln.strip()]
    if not lines:
        raise SystemExit(f"no training lines in {path}")
    return lines


def load_soft_records(path: Path) -> list[dict]:
    records: list[dict] = []
    with open(path) as f:
        for lineno, ln in enumerate(f, 1):
            ln = ln.strip()
            if not ln:
                continue
            try:
                rec = json.loads(ln)
            except json.JSONDecodeError as e:
                raise SystemExit(f"bad JSONL at {path}:{lineno}: {e}") from e
            if "context" not in rec or "action_probs" not in rec:
                raise SystemExit(f"missing context/action_probs at {path}:{lineno}")
            records.append(rec)
    if not records:
        raise SystemExit(f"no soft-label records in {path}")
    return records


def encode_hard_dataset(
    lines: list[str], tokenizer: Tokenizer, block_size: int
) -> tuple[np.ndarray, np.ndarray]:
    pad_id = tokenizer.pad_id
    ignore = -100
    n = len(lines)
    x = np.full((n, block_size), pad_id, dtype=np.int32)
    y = np.full((n, block_size), ignore, dtype=np.int32)

    kept = 0
    skipped = 0
    for line in lines:
        ids = tokenizer.encode(line)
        if len(ids) < 2:
            skipped += 1
            continue
        if len(ids) > block_size + 1:
            ids = ids[: block_size + 1]
        inp = ids[:-1]
        tgt = ids[1:]
        t = len(inp)
        x[kept, :t] = np.asarray(inp, dtype=np.int32)
        y[kept, :t] = np.asarray(tgt, dtype=np.int32)
        kept += 1

    if skipped:
        print(f"skipped {skipped} too-short lines")
    return x[:kept], y[:kept]


def encode_soft_dataset(
    records: list[dict], tokenizer: Tokenizer, block_size: int
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Return x (B,T), last_idx (B,), target_probs (B,V)."""
    pad_id = tokenizer.pad_id
    v = tokenizer.vocab_size
    n = len(records)
    x = np.full((n, block_size), pad_id, dtype=np.int32)
    last_idx = np.zeros((n,), dtype=np.int32)
    y = np.zeros((n, v), dtype=np.float32)

    kept = 0
    skipped = 0
    for rec in records:
        ids = tokenizer.encode(rec["context"])
        if not ids:
            skipped += 1
            continue
        if len(ids) > block_size:
            ids = ids[-block_size:]
        t = len(ids)
        x[kept, :t] = np.asarray(ids, dtype=np.int32)
        last_idx[kept] = t - 1

        probs = np.zeros((v,), dtype=np.float32)
        for tok, p in (rec["action_probs"] or {}).items():
            tid = tokenizer.token_to_id.get(tok)
            if tid is None:
                continue
            probs[tid] += float(p)
        total = float(probs.sum())
        if total <= 0.0:
            skipped += 1
            continue
        y[kept] = probs / total
        kept += 1

    if skipped:
        print(f"skipped {skipped} empty/invalid soft examples")
    return x[:kept], last_idx[:kept], y[:kept]


def batch_iterator_hard(
    x: np.ndarray, y: np.ndarray, batch_size: int, rng: np.random.Generator, shuffle: bool
):
    n = x.shape[0]
    indices = np.arange(n)
    if shuffle:
        rng.shuffle(indices)
    for start in range(0, n, batch_size):
        idx = indices[start : start + batch_size]
        if idx.size == 0:
            break
        yield mx.array(x[idx]), mx.array(y[idx])


def batch_iterator_soft(
    x: np.ndarray,
    last_idx: np.ndarray,
    y: np.ndarray,
    batch_size: int,
    rng: np.random.Generator,
    shuffle: bool,
):
    n = x.shape[0]
    indices = np.arange(n)
    if shuffle:
        rng.shuffle(indices)
    for start in range(0, n, batch_size):
        idx = indices[start : start + batch_size]
        if idx.size == 0:
            break
        yield mx.array(x[idx]), mx.array(last_idx[idx]), mx.array(y[idx])


def cosine_lr(step: int, warmup: int, total: int, base_lr: float) -> float:
    if step < warmup:
        return base_lr * (step + 1) / max(1, warmup)
    if step >= total:
        return base_lr * 0.1
    progress = (step - warmup) / max(1, total - warmup)
    return base_lr * (0.1 + 0.9 * 0.5 * (1.0 + math.cos(math.pi * progress)))


def evaluate_hard(model: GPT, x: np.ndarray, y: np.ndarray, batch_size: int) -> float:
    model.eval()
    losses = []
    n = x.shape[0]
    for start in range(0, n, batch_size):
        xb = mx.array(x[start : start + batch_size])
        yb = mx.array(y[start : start + batch_size])
        loss = model.loss(xb, yb, ignore_index=-100)
        mx.eval(loss)
        losses.append(float(loss))
    model.train()
    return float(np.mean(losses)) if losses else float("nan")


def evaluate_soft(
    model: GPT,
    x: np.ndarray,
    last_idx: np.ndarray,
    y: np.ndarray,
    batch_size: int,
) -> float:
    model.eval()
    losses = []
    n = x.shape[0]
    for start in range(0, n, batch_size):
        xb = mx.array(x[start : start + batch_size])
        lb = mx.array(last_idx[start : start + batch_size])
        yb = mx.array(y[start : start + batch_size])
        loss = model.soft_loss(xb, lb, yb)
        mx.eval(loss)
        losses.append(float(loss))
    model.train()
    return float(np.mean(losses)) if losses else float("nan")


def save_checkpoint(
    out: Path,
    model: GPT,
    tokenizer: Tokenizer,
    config: GPTConfig,
    meta: dict,
) -> None:
    out.mkdir(parents=True, exist_ok=True)
    tokenizer.save(out / "tokenizer.json")
    with open(out / "config.json", "w") as f:
        json.dump(config.to_dict(), f, indent=2)
    with open(out / "meta.json", "w") as f:
        json.dump(meta, f, indent=2)

    flat = {k: np.array(v) for k, v in tree_flatten(model.parameters())}
    np.savez(out / "weights.npz", **{k.replace(".", "__"): v for k, v in flat.items()})


def main() -> None:
    args = parse_args()
    mode = resolve_mode(args)
    mx.random.seed(args.seed)
    rng = np.random.default_rng(args.seed)

    print(f"device: {mx.default_device()}")
    print(f"mode: {mode}")
    print(f"loading {args.data}")

    if mode == "soft":
        records = load_soft_records(args.data)
        print(f"records: {len(records):,}")
        print("building tokenizer...")
        tokenizer = Tokenizer.build_from_soft(records)
        print(f"vocab_size: {tokenizer.vocab_size} (includes {PAD})")
        print("encoding dataset...")
        x_all, last_all, y_all = encode_soft_dataset(records, tokenizer, args.block_size)
        del records
    else:
        lines = load_lines(args.data)
        print(f"lines: {len(lines):,}")
        print("building tokenizer...")
        tokenizer = Tokenizer.build(lines)
        print(f"vocab_size: {tokenizer.vocab_size} (includes {PAD})")
        print("encoding dataset...")
        x_all, y_all = encode_hard_dataset(lines, tokenizer, args.block_size)
        last_all = None
        del lines

    n = x_all.shape[0]
    n_val = max(1, int(n * args.val_fraction))
    n_train = n - n_val
    perm = rng.permutation(n)
    train_idx, val_idx = perm[:n_train], perm[n_train:]
    x_train, x_val = x_all[train_idx], x_all[val_idx]
    y_train, y_val = y_all[train_idx], y_all[val_idx]
    if mode == "soft":
        last_train, last_val = last_all[train_idx], last_all[val_idx]
    print(f"train: {len(x_train):,}  val: {len(x_val):,}")

    config = GPTConfig(
        vocab_size=tokenizer.vocab_size,
        block_size=args.block_size,
        n_layer=args.n_layer,
        n_head=args.n_head,
        n_embd=args.n_embd,
        dropout=args.dropout,
    )
    model = GPT(config)
    mx.eval(model.parameters())
    n_params = sum(int(np.prod(p.shape)) for _, p in tree_flatten(model.parameters()))
    print(f"params: {n_params/1e6:.2f}M")
    print(f"config: {config.to_dict()}")

    optimizer = optim.AdamW(learning_rate=args.lr, weight_decay=args.weight_decay)

    if mode == "soft":

        def loss_fn(model: GPT, xb: mx.array, lb: mx.array, yb: mx.array):
            return model.soft_loss(xb, lb, yb)

        loss_and_grad = nn.value_and_grad(model, loss_fn)
    else:

        def loss_fn(model: GPT, xb: mx.array, yb: mx.array):
            return model.loss(xb, yb, ignore_index=-100)

        loss_and_grad = nn.value_and_grad(model, loss_fn)

    steps_per_epoch = math.ceil(len(x_train) / args.batch_size)
    if args.max_batches > 0:
        steps_per_epoch = min(steps_per_epoch, args.max_batches)
    total_steps = steps_per_epoch * args.epochs
    print(f"steps/epoch: {steps_per_epoch}  total_steps: {total_steps}")

    best_val = float("inf")
    step = 0
    t0 = time.time()

    for epoch in range(args.epochs):
        model.train()
        running = []
        if mode == "soft":
            batches = batch_iterator_soft(
                x_train, last_train, y_train, args.batch_size, rng, shuffle=True
            )
        else:
            batches = batch_iterator_hard(x_train, y_train, args.batch_size, rng, shuffle=True)

        for bi, batch in enumerate(batches):
            if args.max_batches and bi >= args.max_batches:
                break

            lr = cosine_lr(step, args.warmup_steps, total_steps, args.lr)
            optimizer.learning_rate = lr

            if mode == "soft":
                xb, lb, yb = batch
                loss, grads = loss_and_grad(model, xb, lb, yb)
            else:
                xb, yb = batch
                loss, grads = loss_and_grad(model, xb, yb)

            optimizer.update(model, grads)
            mx.eval(model.parameters(), optimizer.state, loss)

            running.append(float(loss))
            step += 1

            if step % args.log_every == 0:
                dt = time.time() - t0
                print(
                    f"epoch {epoch+1}/{args.epochs} step {step}/{total_steps} "
                    f"loss {np.mean(running):.4f} lr {lr:.2e} ({dt:.1f}s)"
                )
                running = []
                t0 = time.time()

            if step % args.eval_every == 0 or step == total_steps:
                if mode == "soft":
                    val_loss = evaluate_soft(model, x_val, last_val, y_val, args.batch_size)
                else:
                    val_loss = evaluate_hard(model, x_val, y_val, args.batch_size)
                print(f"  val_loss {val_loss:.4f}")
                meta = {
                    "step": step,
                    "epoch": epoch + 1,
                    "val_loss": val_loss,
                    "train_lines": int(len(x_train)),
                    "val_lines": int(len(x_val)),
                    "mode": mode,
                }
                save_checkpoint(args.out / "last", model, tokenizer, config, meta)
                if val_loss < best_val:
                    best_val = val_loss
                    save_checkpoint(args.out / "best", model, tokenizer, config, meta)
                    print(f"  saved best -> {args.out / 'best'}")

    if mode == "soft":
        val_loss = evaluate_soft(model, x_val, last_val, y_val, args.batch_size)
    else:
        val_loss = evaluate_hard(model, x_val, y_val, args.batch_size)
    meta = {"step": step, "epoch": args.epochs, "val_loss": val_loss, "mode": mode}
    save_checkpoint(args.out / "last", model, tokenizer, config, meta)
    print(f"done. best_val={best_val:.4f}  checkpoints in {args.out}")


if __name__ == "__main__":
    main()
