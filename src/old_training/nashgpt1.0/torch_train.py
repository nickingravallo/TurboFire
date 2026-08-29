#!/usr/bin/env python3
"""PyTorch soft-label trainer for TurboFire (CUDA / Linux server port).

Streams JSONL -> memory-mapped arrays so 31M rows fit on ~32–64GB RAM machines.

Example (on the server):
  pip install -r requirements-torch.txt
  python torch_train.py --data ../training_soft.jsonl --out out_torch --epochs 3

Smoke:
  python torch_train.py --data ../training_soft.jsonl --out out_torch_smoke \\
    --epochs 1 --max-batches 50
"""

from __future__ import annotations

import argparse
import json
import math
import time
from pathlib import Path

import numpy as np
import torch
from torch.utils.data import DataLoader, Dataset, Subset

from tokenizer import PAD, Tokenizer
from torch_model import GPT, GPTConfig


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="PyTorch soft-label TurboFire trainer")
    p.add_argument("--data", type=Path, required=True)
    p.add_argument("--out", type=Path, default=Path("out_torch"))
    p.add_argument("--cache", type=Path, default=Path("cache_soft"), help="memmap dir")
    p.add_argument("--block-size", type=int, default=32)
    p.add_argument("--n-layer", type=int, default=6)
    p.add_argument("--n-head", type=int, default=6)
    p.add_argument("--n-embd", type=int, default=384)
    p.add_argument("--dropout", type=float, default=0.0)
    p.add_argument("--batch-size", type=int, default=512)
    p.add_argument("--epochs", type=int, default=3)
    p.add_argument("--lr", type=float, default=3e-4)
    p.add_argument("--weight-decay", type=float, default=0.1)
    p.add_argument("--warmup-steps", type=int, default=200)
    p.add_argument("--val-fraction", type=float, default=0.02)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--log-every", type=int, default=50)
    p.add_argument("--eval-every", type=int, default=500)
    p.add_argument("--max-batches", type=int, default=0)
    p.add_argument("--num-workers", type=int, default=2)
    p.add_argument("--rebuild-cache", action="store_true")
    return p.parse_args()


def count_lines(path: Path) -> int:
    n = 0
    with path.open() as f:
        for line in f:
            if line.strip():
                n += 1
    return n


def build_tokenizer_streaming(path: Path) -> Tokenizer:
    """One pass over JSONL to build vocab (seeded + observed tokens)."""
    from tokenizer import seed_tokens

    tokens = seed_tokens()
    with path.open() as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            tokens.update(str(rec.get("context", "")).split())
            tokens.update((rec.get("action_probs") or {}).keys())
    return Tokenizer._from_token_set(tokens)


def encode_to_memmap(
    path: Path,
    tokenizer: Tokenizer,
    block_size: int,
    cache: Path,
) -> tuple[np.memmap, np.memmap, np.memmap, int]:
    """Stream JSONL into memmaps. Returns x, last_idx, y, n_kept."""
    cache.mkdir(parents=True, exist_ok=True)
    meta_path = cache / "meta.json"
    x_path, last_path, y_path = cache / "x.npy", cache / "last.npy", cache / "y.npy"

    n_lines = count_lines(path)
    v = tokenizer.vocab_size
    print(f"encoding {n_lines:,} lines -> {cache} (vocab={v})")

    x = np.lib.format.open_memmap(
        x_path, mode="w+", dtype=np.int32, shape=(n_lines, block_size)
    )
    last = np.lib.format.open_memmap(
        last_path, mode="w+", dtype=np.int32, shape=(n_lines,)
    )
    y = np.lib.format.open_memmap(
        y_path, mode="w+", dtype=np.float32, shape=(n_lines, v)
    )
    x[:] = tokenizer.pad_id
    last[:] = 0
    y[:] = 0.0

    kept = 0
    skipped = 0
    with path.open() as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            ids = tokenizer.encode(rec["context"])
            if not ids:
                skipped += 1
                continue
            if len(ids) > block_size:
                ids = ids[-block_size:]
            t = len(ids)
            x[kept, :t] = np.asarray(ids, dtype=np.int32)
            last[kept] = t - 1
            probs = np.zeros((v,), dtype=np.float32)
            for tok, p in (rec.get("action_probs") or {}).items():
                tid = tokenizer.token_to_id.get(tok)
                if tid is not None:
                    probs[tid] += float(p)
            total = float(probs.sum())
            if total <= 0.0:
                skipped += 1
                continue
            y[kept] = probs / total
            kept += 1
            if kept % 2_000_000 == 0:
                print(f"  encoded {kept:,}...")

    x.flush()
    last.flush()
    y.flush()
    # Shrink by rewriting kept slice into final files
    print(f"compacting kept={kept:,} skipped={skipped:,}")
    x_final = np.lib.format.open_memmap(
        cache / "x_final.npy", mode="w+", dtype=np.int32, shape=(kept, block_size)
    )
    last_final = np.lib.format.open_memmap(
        cache / "last_final.npy", mode="w+", dtype=np.int32, shape=(kept,)
    )
    y_final = np.lib.format.open_memmap(
        cache / "y_final.npy", mode="w+", dtype=np.float32, shape=(kept, v)
    )
    # copy in chunks
    chunk = 1_000_000
    for start in range(0, kept, chunk):
        end = min(kept, start + chunk)
        x_final[start:end] = x[start:end]
        last_final[start:end] = last[start:end]
        y_final[start:end] = y[start:end]
    x_final.flush()
    last_final.flush()
    y_final.flush()
    del x, last, y
    for p in (x_path, last_path, y_path):
        p.unlink(missing_ok=True)
    (cache / "x_final.npy").replace(x_path)
    (cache / "last_final.npy").replace(last_path)
    (cache / "y_final.npy").replace(y_path)

    meta = {
        "n": kept,
        "block_size": block_size,
        "vocab_size": v,
        "data": str(path),
    }
    meta_path.write_text(json.dumps(meta, indent=2))
    tokenizer.save(cache / "tokenizer.json")

    x = np.load(x_path, mmap_mode="r")
    last = np.load(last_path, mmap_mode="r")
    y = np.load(y_path, mmap_mode="r")
    return x, last, y, kept


def load_memmap_cache(cache: Path) -> tuple[np.memmap, np.memmap, np.memmap, Tokenizer, dict]:
    meta = json.loads((cache / "meta.json").read_text())
    tokenizer = Tokenizer.from_file(cache / "tokenizer.json")
    x = np.load(cache / "x.npy", mmap_mode="r")
    last = np.load(cache / "last.npy", mmap_mode="r")
    y = np.load(cache / "y.npy", mmap_mode="r")
    return x, last, y, tokenizer, meta


class SoftMemmapDataset(Dataset):
    def __init__(self, x: np.memmap, last: np.memmap, y: np.memmap):
        self.x = x
        self.last = last
        self.y = y

    def __len__(self) -> int:
        return int(self.x.shape[0])

    def __getitem__(self, i: int):
        return (
            torch.from_numpy(np.array(self.x[i], copy=True)),
            torch.tensor(int(self.last[i]), dtype=torch.long),
            torch.from_numpy(np.array(self.y[i], copy=True)),
        )


def cosine_lr(step: int, warmup: int, total: int, base_lr: float) -> float:
    if step < warmup:
        return base_lr * (step + 1) / max(1, warmup)
    if step >= total:
        return base_lr * 0.1
    progress = (step - warmup) / max(1, total - warmup)
    return base_lr * (0.1 + 0.9 * 0.5 * (1.0 + math.cos(math.pi * progress)))


@torch.no_grad()
def evaluate(model: GPT, loader: DataLoader, device: torch.device) -> float:
    model.eval()
    total = 0.0
    n = 0
    for xb, lb, yb in loader:
        xb = xb.to(device)
        lb = lb.to(device)
        yb = yb.to(device)
        loss = model.soft_loss(xb, lb, yb)
        total += float(loss) * xb.size(0)
        n += xb.size(0)
    model.train()
    return total / max(n, 1)


def save_checkpoint(
    out: Path, model: GPT, tokenizer: Tokenizer, config: GPTConfig, meta: dict
) -> None:
    out.mkdir(parents=True, exist_ok=True)
    tokenizer.save(out / "tokenizer.json")
    (out / "config.json").write_text(json.dumps(config.to_dict(), indent=2))
    (out / "meta.json").write_text(json.dumps(meta, indent=2))
    torch.save(model.state_dict(), out / "weights.pt")


def main() -> None:
    args = parse_args()
    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"device: {device}")
    if device.type == "cuda":
        print(f"gpu: {torch.cuda.get_device_name(0)}")

    cache_ready = (args.cache / "meta.json").exists() and not args.rebuild_cache
    if cache_ready:
        print(f"loading cache {args.cache}")
        x, last, y, tokenizer, meta = load_memmap_cache(args.cache)
        n = int(meta["n"])
    else:
        print("building tokenizer (streaming)...")
        tokenizer = build_tokenizer_streaming(args.data)
        print(f"vocab_size: {tokenizer.vocab_size} (includes {PAD})")
        x, last, y, n = encode_to_memmap(
            args.data, tokenizer, args.block_size, args.cache
        )

    print(f"examples: {n:,}")
    ds = SoftMemmapDataset(x, last, y)
    n_val = max(1, int(n * args.val_fraction))
    n_train = n - n_val
    rng = np.random.default_rng(args.seed)
    perm = rng.permutation(n)
    train_ds = Subset(ds, perm[:n_train].tolist())
    val_ds = Subset(ds, perm[n_train:].tolist())
    print(f"train: {len(train_ds):,}  val: {len(val_ds):,}")

    train_loader = DataLoader(
        train_ds,
        batch_size=args.batch_size,
        shuffle=True,
        num_workers=args.num_workers,
        pin_memory=(device.type == "cuda"),
    )
    val_loader = DataLoader(
        val_ds,
        batch_size=args.batch_size,
        shuffle=False,
        num_workers=args.num_workers,
        pin_memory=(device.type == "cuda"),
    )

    config = GPTConfig(
        vocab_size=tokenizer.vocab_size,
        block_size=args.block_size,
        n_layer=args.n_layer,
        n_head=args.n_head,
        n_embd=args.n_embd,
        dropout=args.dropout,
    )
    model = GPT(config).to(device)
    n_params = sum(p.numel() for p in model.parameters())
    print(f"params: {n_params/1e6:.2f}M  config: {config.to_dict()}")

    opt = torch.optim.AdamW(
        model.parameters(), lr=args.lr, weight_decay=args.weight_decay
    )

    steps_per_epoch = math.ceil(len(train_ds) / args.batch_size)
    if args.max_batches > 0:
        steps_per_epoch = min(steps_per_epoch, args.max_batches)
    total_steps = steps_per_epoch * args.epochs
    print(f"steps/epoch: {steps_per_epoch}  total_steps: {total_steps}")

    best_val = float("inf")
    step = 0
    t0 = time.time()
    model.train()

    for epoch in range(args.epochs):
        running = []
        for bi, (xb, lb, yb) in enumerate(train_loader):
            if args.max_batches and bi >= args.max_batches:
                break
            lr = cosine_lr(step, args.warmup_steps, total_steps, args.lr)
            for g in opt.param_groups:
                g["lr"] = lr

            xb = xb.to(device, non_blocking=True)
            lb = lb.to(device, non_blocking=True)
            yb = yb.to(device, non_blocking=True)
            loss = model.soft_loss(xb, lb, yb)
            opt.zero_grad(set_to_none=True)
            loss.backward()
            opt.step()

            running.append(float(loss.detach()))
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
                val_loss = evaluate(model, val_loader, device)
                print(f"  val_loss {val_loss:.4f}")
                meta = {
                    "step": step,
                    "epoch": epoch + 1,
                    "val_loss": val_loss,
                    "train_lines": len(train_ds),
                    "val_lines": len(val_ds),
                    "mode": "soft-torch",
                }
                save_checkpoint(args.out / "last", model, tokenizer, config, meta)
                if val_loss < best_val:
                    best_val = val_loss
                    save_checkpoint(args.out / "best", model, tokenizer, config, meta)
                    print(f"  saved best -> {args.out / 'best'}")

    val_loss = evaluate(model, val_loader, device)
    save_checkpoint(
        args.out / "last",
        model,
        tokenizer,
        config,
        {"step": step, "epoch": args.epochs, "val_loss": val_loss, "mode": "soft-torch"},
    )
    print(f"done. best_val={best_val:.4f}  checkpoints in {args.out}")


if __name__ == "__main__":
    main()
