#!/usr/bin/env python3
"""PyTorch NashGPT soft-label trainer (CUDA / Linux server port).

NashGPT 1.7 splits rank/suit tokens, permutes suits on train rows only, and
reweights difficult mixed nodes while retaining the 1.6 solver labels.

  python torch_train.py --cache cache_soft_17 --out out_torch_17
"""

from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import time
from pathlib import Path

import numpy as np
import torch
from torch.utils.data import DataLoader, Dataset, Subset, WeightedRandomSampler

from card_tokens import TOKEN_FORMAT_SPLIT, TOKEN_FORMATS, normalize_context
from flop_split import split_by_flop, split_by_rows, write_holdout_flops
from legal import (
    LEGAL_PROFILES,
    PROFILE_NASHGPT16,
    kind_masks,
    node_kind_from_context,
)
from nashgpt17 import (
    NON_IDENTITY_PERMS,
    REPRESENTATION_VERSION,
    sampling_weights,
    suit_id_lookups,
)
from tokenizer import PAD, Tokenizer
from torch_model import GPT, GPTConfig

CACHE_VERSION = 2


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="PyTorch soft-label TurboFire trainer")
    p.add_argument(
        "--data",
        type=Path,
        default=Path("../training_soft_nashgpt16_condensed.jsonl"),
    )
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
    p.add_argument(
        "--split",
        choices=("rows", "flops"),
        default="flops",
        help="rows: random examples. flops: hold out entire flop textures",
    )
    p.add_argument(
        "--holdout-flops",
        type=int,
        default=155,
        help="distinct flops reserved for val when --split flops",
    )
    p.add_argument("--val-fraction", type=float, default=0.02)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--log-every", type=int, default=50)
    p.add_argument("--eval-every", type=int, default=500)
    p.add_argument("--max-batches", type=int, default=0)
    p.add_argument("--num-workers", type=int, default=2)
    p.add_argument("--rebuild-cache", action="store_true")
    p.add_argument(
        "--token-format",
        choices=TOKEN_FORMATS,
        default=TOKEN_FORMAT_SPLIT,
        help="Model card representation; 1.7 uses split_rank_suit",
    )
    p.add_argument(
        "--suit-permute-prob",
        type=float,
        default=0.5,
        help="Train-row probability of a random non-identity suit permutation",
    )
    p.add_argument(
        "--facing-bet-weight",
        type=float,
        default=2.0,
        help="Sampling multiplier for facing-bet rows",
    )
    p.add_argument(
        "--entropy-weight",
        type=float,
        default=1.0,
        help="Sampling factor coefficient in 1 + coefficient * label entropy",
    )
    p.add_argument(
        "--legal-profile",
        choices=LEGAL_PROFILES,
        default=PROFILE_NASHGPT16,
        help="Legal action abstraction used by the dataset",
    )
    return p.parse_args()


def count_lines(path: Path) -> int:
    n = 0
    with path.open() as f:
        for line in f:
            if line.strip():
                n += 1
    return n


def source_revision() -> str:
    configured = os.environ.get("TURBOFIRE_SOURCE_REVISION")
    if configured:
        return configured
    try:
        root = Path(__file__).resolve().parents[2]
        return subprocess.check_output(
            ["git", "rev-parse", "HEAD"],
            cwd=root,
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def build_tokenizer_streaming(path: Path, token_format: str) -> Tokenizer:
    """One pass over JSONL to build vocab (seeded + observed tokens)."""
    from tokenizer import seed_tokens

    tokens = seed_tokens(token_format)
    with path.open() as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            context = normalize_context(str(rec.get("context", "")), token_format)
            tokens.update(context.split())
            tokens.update((rec.get("action_probs") or {}).keys())
    return Tokenizer._from_token_set(tokens, token_format)


def encode_to_memmap(
    path: Path,
    tokenizer: Tokenizer,
    block_size: int,
    cache: Path,
    legal_profile: str,
) -> tuple[np.memmap, np.memmap, np.memmap, np.memmap, int]:
    """Stream JSONL into memmaps. Returns x, last_idx, y, kind, n_kept."""
    cache.mkdir(parents=True, exist_ok=True)
    meta_path = cache / "meta.json"
    x_path = cache / "x.npy"
    last_path = cache / "last.npy"
    y_path = cache / "y.npy"
    kind_path = cache / "kind.npy"

    n_lines = count_lines(path)
    v = tokenizer.vocab_size
    masks = kind_masks(tokenizer, legal_profile)
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
    kinds = np.lib.format.open_memmap(
        kind_path, mode="w+", dtype=np.uint8, shape=(n_lines,)
    )
    x[:] = tokenizer.pad_id
    last[:] = 0
    y[:] = 0.0
    kinds[:] = 0

    kept = 0
    skipped = 0
    illegal_target = 0
    with path.open() as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            ctx = rec["context"]
            ids = tokenizer.encode(ctx)
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
            kind = node_kind_from_context(ctx, legal_profile)
            if not masks[kind][probs > 0].all():
                illegal_target += 1
                continue
            y[kept] = probs / total
            kinds[kept] = kind
            kept += 1
            if kept % 2_000_000 == 0:
                print(f"  encoded {kept:,}...")

    x.flush()
    last.flush()
    y.flush()
    kinds.flush()
    # Shrink by rewriting kept slice into final files
    print(
        f"compacting kept={kept:,} skipped={skipped:,} "
        f"target_off_legal={illegal_target:,}"
    )
    x_final = np.lib.format.open_memmap(
        cache / "x_final.npy", mode="w+", dtype=np.int32, shape=(kept, block_size)
    )
    last_final = np.lib.format.open_memmap(
        cache / "last_final.npy", mode="w+", dtype=np.int32, shape=(kept,)
    )
    y_final = np.lib.format.open_memmap(
        cache / "y_final.npy", mode="w+", dtype=np.float32, shape=(kept, v)
    )
    kind_final = np.lib.format.open_memmap(
        cache / "kind_final.npy", mode="w+", dtype=np.uint8, shape=(kept,)
    )
    # copy in chunks
    chunk = 1_000_000
    for start in range(0, kept, chunk):
        end = min(kept, start + chunk)
        x_final[start:end] = x[start:end]
        last_final[start:end] = last[start:end]
        y_final[start:end] = y[start:end]
        kind_final[start:end] = kinds[start:end]
    x_final.flush()
    last_final.flush()
    y_final.flush()
    kind_final.flush()
    del x, last, y, kinds
    for p in (x_path, last_path, y_path, kind_path):
        p.unlink(missing_ok=True)
    (cache / "x_final.npy").replace(x_path)
    (cache / "last_final.npy").replace(last_path)
    (cache / "y_final.npy").replace(y_path)
    (cache / "kind_final.npy").replace(kind_path)

    meta = {
        "n": kept,
        "block_size": block_size,
        "vocab_size": v,
        "data": str(path.resolve()),
        "data_size": path.stat().st_size,
        "data_mtime_ns": path.stat().st_mtime_ns,
        "legal_profile": legal_profile,
        "token_format": tokenizer.token_format,
        "cache_version": CACHE_VERSION,
        "representation_version": REPRESENTATION_VERSION,
    }
    meta_path.write_text(json.dumps(meta, indent=2))
    tokenizer.save(cache / "tokenizer.json")

    x = np.load(x_path, mmap_mode="r")
    last = np.load(last_path, mmap_mode="r")
    y = np.load(y_path, mmap_mode="r")
    kinds = np.load(kind_path, mmap_mode="r")
    return x, last, y, kinds, kept


def load_memmap_cache(
    cache: Path,
) -> tuple[np.memmap, np.memmap, np.memmap, np.memmap, Tokenizer, dict]:
    meta = json.loads((cache / "meta.json").read_text())
    tokenizer = Tokenizer.from_file(cache / "tokenizer.json")
    x = np.load(cache / "x.npy", mmap_mode="r")
    last = np.load(cache / "last.npy", mmap_mode="r")
    y = np.load(cache / "y.npy", mmap_mode="r")
    kinds = np.load(cache / "kind.npy", mmap_mode="r")
    return x, last, y, kinds, tokenizer, meta


class SoftMemmapDataset(Dataset):
    def __init__(
        self,
        x: np.memmap,
        last: np.memmap,
        y: np.memmap,
        kinds: np.memmap,
        tokenizer: Tokenizer,
        legal_profile: str,
    ):
        self.x = x
        self.last = last
        self.y = y
        self.kinds = kinds
        self.tokenizer = tokenizer
        self.legal_profile = legal_profile
        self._kind_masks = torch.from_numpy(kind_masks(tokenizer, legal_profile))

    def __len__(self) -> int:
        return int(self.x.shape[0])

    def __getitem__(self, i: int):
        ids = np.array(self.x[i], copy=True)
        kind = int(self.kinds[i])
        return (
            torch.from_numpy(ids),
            torch.tensor(int(self.last[i]), dtype=torch.long),
            torch.from_numpy(np.array(self.y[i], copy=True)),
            self._kind_masks[kind].clone(),
        )


class SuitPermutationDataset(Dataset):
    """Apply global suit relabeling to train rows without touching validation."""

    def __init__(
        self,
        base: Dataset,
        tokenizer: Tokenizer,
        probability: float,
    ):
        if not 0.0 <= probability <= 1.0:
            raise ValueError("--suit-permute-prob must be in [0, 1]")
        self.base = base
        self.probability = probability
        self.lookups = torch.from_numpy(suit_id_lookups(tokenizer))

    def __len__(self) -> int:
        return len(self.base)

    def __getitem__(self, i: int):
        ids, last, target, mask = self.base[i]
        if self.probability > 0.0 and torch.rand(()) < self.probability:
            choice = int(torch.randint(len(NON_IDENTITY_PERMS), (1,)))
            ids = self.lookups[choice][ids.long()].to(ids.dtype)
        return ids, last, target, mask


def build_train_weights(
    y: np.memmap,
    kinds: np.memmap,
    train_idx: np.ndarray,
    facing_bet_weight: float,
    entropy_weight: float,
    chunk: int = 100_000,
) -> torch.Tensor:
    weights = np.empty(len(train_idx), dtype=np.float64)
    for start in range(0, len(train_idx), chunk):
        end = min(len(train_idx), start + chunk)
        idx = train_idx[start:end]
        weights[start:end] = sampling_weights(
            np.asarray(y[idx]),
            np.asarray(kinds[idx]),
            facing_bet_weight,
            entropy_weight,
        )
    return torch.from_numpy(weights)


def cache_matches(meta: dict, args: argparse.Namespace) -> tuple[bool, str]:
    required = {
        "data": str(args.data.resolve()),
        "data_size": args.data.stat().st_size,
        "data_mtime_ns": args.data.stat().st_mtime_ns,
        "block_size": args.block_size,
        "legal_profile": args.legal_profile,
        "token_format": args.token_format,
        "cache_version": CACHE_VERSION,
        "representation_version": REPRESENTATION_VERSION,
    }
    for key, expected in required.items():
        if meta.get(key) != expected:
            return False, f"{key}={meta.get(key)!r}, expected {expected!r}"
    if not (args.cache / "kind.npy").exists():
        return False, "kind.npy missing"
    return True, ""


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
    for xb, lb, yb, mb in loader:
        xb = xb.to(device)
        lb = lb.to(device)
        yb = yb.to(device)
        mb = mb.to(device)
        loss = model.soft_loss(xb, lb, yb, mb)
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
    if not 0.0 <= args.suit_permute_prob <= 1.0:
        raise SystemExit("--suit-permute-prob must be in [0, 1]")
    if args.token_format != TOKEN_FORMAT_SPLIT and args.suit_permute_prob:
        raise SystemExit("suit permutation requires --token-format split_rank_suit")
    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    revision = source_revision()
    print(f"device: {device}")
    print(f"nashgpt legal profile: {args.legal_profile}")
    print(
        f"token_format={args.token_format} suit_permute_prob={args.suit_permute_prob} "
        f"facing_bet_weight={args.facing_bet_weight} entropy_weight={args.entropy_weight}"
    )
    print("soft CE: softmax masked to legal HERO actions")
    if device.type == "cuda":
        print(f"gpu: {torch.cuda.get_device_name(0)}")

    cache_ready = (args.cache / "meta.json").exists() and not args.rebuild_cache
    if cache_ready:
        cache_meta = json.loads((args.cache / "meta.json").read_text())
        cache_ready, reason = cache_matches(cache_meta, args)
        if not cache_ready:
            print(f"cache incompatible ({reason}); rebuilding")
            cache_ready = False
    if cache_ready:
        print(f"loading cache {args.cache}")
        x, last, y, kinds, tokenizer, meta = load_memmap_cache(args.cache)
        if tokenizer.vocab_size != int(meta["vocab_size"]):
            raise SystemExit("cache tokenizer vocabulary does not match cache metadata")
        n = int(meta["n"])
    else:
        print("building tokenizer (streaming)...")
        tokenizer = build_tokenizer_streaming(args.data, args.token_format)
        print(f"vocab_size: {tokenizer.vocab_size} (includes {PAD})")
        x, last, y, kinds, n = encode_to_memmap(
            args.data,
            tokenizer,
            args.block_size,
            args.cache,
            args.legal_profile,
        )

    print(f"examples: {n:,}")
    ds = SoftMemmapDataset(x, last, y, kinds, tokenizer, args.legal_profile)
    if args.split == "flops":
        train_idx, val_idx, split_info = split_by_flop(
            x, tokenizer, args.seed, args.holdout_flops
        )
        write_holdout_flops(args.out / "holdout_flops.txt", split_info["holdout_flops"])
        print(
            f"split: flop holdout  "
            f"{split_info['flops_train']} train / {split_info['flops_holdout']} holdout "
            f"({split_info['flops_total']} textures)  seed={args.seed}"
        )
        print(f"  holdout list -> {args.out / 'holdout_flops.txt'}")
    else:
        train_idx, val_idx, split_info = split_by_rows(n, args.seed, args.val_fraction)
        print(f"split: random rows  val_fraction={args.val_fraction}")
    train_base = Subset(ds, train_idx.tolist())
    train_ds = SuitPermutationDataset(
        train_base, tokenizer, args.suit_permute_prob
    )
    val_ds = Subset(ds, val_idx.tolist())
    print(f"train: {len(train_ds):,}  val: {len(val_ds):,}")

    print("building mixed-node sampling weights...")
    train_weights = build_train_weights(
        y,
        kinds,
        train_idx,
        args.facing_bet_weight,
        args.entropy_weight,
    )
    sampler = WeightedRandomSampler(
        train_weights,
        num_samples=len(train_ds),
        replacement=True,
        generator=torch.Generator().manual_seed(args.seed),
    )
    train_loader = DataLoader(
        train_ds,
        batch_size=args.batch_size,
        sampler=sampler,
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
        for bi, (xb, lb, yb, mb) in enumerate(train_loader):
            if args.max_batches and bi >= args.max_batches:
                break
            lr = cosine_lr(step, args.warmup_steps, total_steps, args.lr)
            for g in opt.param_groups:
                g["lr"] = lr

            xb = xb.to(device, non_blocking=True)
            lb = lb.to(device, non_blocking=True)
            yb = yb.to(device, non_blocking=True)
            mb = mb.to(device, non_blocking=True)
            loss = model.soft_loss(xb, lb, yb, mb)
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
                    "mode": "soft-torch-legal",
                    "legal_profile": args.legal_profile,
                    "token_format": args.token_format,
                    "suit_permute_probability": args.suit_permute_prob,
                    "facing_bet_weight": args.facing_bet_weight,
                    "entropy_weight": args.entropy_weight,
                    "requires_canonicalization": True,
                    "data": str(args.data.resolve()),
                    "source_revision": revision,
                    "representation_version": REPRESENTATION_VERSION,
                    "split": split_info,
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
        {
            "step": step,
            "epoch": args.epochs,
            "val_loss": val_loss,
            "mode": "soft-torch-legal",
            "legal_profile": args.legal_profile,
            "token_format": args.token_format,
            "suit_permute_probability": args.suit_permute_prob,
            "facing_bet_weight": args.facing_bet_weight,
            "entropy_weight": args.entropy_weight,
            "requires_canonicalization": True,
            "data": str(args.data.resolve()),
            "source_revision": revision,
            "representation_version": REPRESENTATION_VERSION,
            "split": split_info,
        },
    )
    print(f"done. best_val={best_val:.4f}  checkpoints in {args.out}")


if __name__ == "__main__":
    main()
