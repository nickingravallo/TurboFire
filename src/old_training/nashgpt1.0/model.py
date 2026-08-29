"""Tiny causal GPT implemented in MLX.

Sized for TurboFire sequences (vocab ~2k, length ~12–24 tokens) and a
16GB M3 MacBook Air. Default config is ~8M params.
"""

from __future__ import annotations

import math
from dataclasses import asdict, dataclass

import mlx.core as mx
import mlx.nn as nn


@dataclass
class GPTConfig:
    vocab_size: int = 2048
    block_size: int = 32
    n_layer: int = 6
    n_head: int = 6
    n_embd: int = 384
    dropout: float = 0.0  # keep 0 for small data / deterministic runs

    def to_dict(self) -> dict:
        return asdict(self)

    @classmethod
    def from_dict(cls, d: dict) -> "GPTConfig":
        return cls(**{k: d[k] for k in cls.__dataclass_fields__ if k in d})


class CausalSelfAttention(nn.Module):
    def __init__(self, config: GPTConfig):
        super().__init__()
        assert config.n_embd % config.n_head == 0
        self.n_head = config.n_head
        self.n_embd = config.n_embd
        self.head_dim = config.n_embd // config.n_head

        self.c_attn = nn.Linear(config.n_embd, 3 * config.n_embd, bias=False)
        self.c_proj = nn.Linear(config.n_embd, config.n_embd, bias=False)
        self.attn_dropout = nn.Dropout(config.dropout)
        self.resid_dropout = nn.Dropout(config.dropout)

        # Lower-triangular causal mask: position i may only attend to <= i.
        mask = mx.tril(mx.ones((config.block_size, config.block_size)))
        self._causal_mask = mask.reshape(1, 1, config.block_size, config.block_size)

    def __call__(self, x: mx.array) -> mx.array:
        b, t, c = x.shape
        qkv = self.c_attn(x)
        q, k, v = mx.split(qkv, 3, axis=-1)

        def reshape_heads(tensor: mx.array) -> mx.array:
            return tensor.reshape(b, t, self.n_head, self.head_dim).transpose(0, 2, 1, 3)

        q, k, v = map(reshape_heads, (q, k, v))

        scale = 1.0 / math.sqrt(self.head_dim)
        att = (q @ k.transpose(0, 1, 3, 2)) * scale
        att = mx.where(self._causal_mask[:, :, :t, :t] == 0, -1e9, att)
        att = mx.softmax(att, axis=-1)
        att = self.attn_dropout(att)

        y = att @ v
        y = y.transpose(0, 2, 1, 3).reshape(b, t, c)
        return self.resid_dropout(self.c_proj(y))


class MLP(nn.Module):
    def __init__(self, config: GPTConfig):
        super().__init__()
        self.fc = nn.Linear(config.n_embd, 4 * config.n_embd, bias=False)
        self.proj = nn.Linear(4 * config.n_embd, config.n_embd, bias=False)
        self.dropout = nn.Dropout(config.dropout)

    def __call__(self, x: mx.array) -> mx.array:
        return self.dropout(self.proj(nn.gelu(self.fc(x))))


class Block(nn.Module):
    def __init__(self, config: GPTConfig):
        super().__init__()
        self.ln1 = nn.LayerNorm(config.n_embd)
        self.attn = CausalSelfAttention(config)
        self.ln2 = nn.LayerNorm(config.n_embd)
        self.mlp = MLP(config)

    def __call__(self, x: mx.array) -> mx.array:
        x = x + self.attn(self.ln1(x))
        x = x + self.mlp(self.ln2(x))
        return x


class GPT(nn.Module):
    def __init__(self, config: GPTConfig):
        super().__init__()
        self.config = config
        self.wte = nn.Embedding(config.vocab_size, config.n_embd)
        self.wpe = nn.Embedding(config.block_size, config.n_embd)
        self.drop = nn.Dropout(config.dropout)
        self.blocks = [Block(config) for _ in range(config.n_layer)]
        self.ln_f = nn.LayerNorm(config.n_embd)

    def __call__(self, idx: mx.array) -> mx.array:
        _b, t = idx.shape
        if t > self.config.block_size:
            raise ValueError(f"sequence length {t} > block_size {self.config.block_size}")

        pos = mx.arange(t)
        x = self.drop(self.wte(idx) + self.wpe(pos))
        for block in self.blocks:
            x = block(x)
        x = self.ln_f(x)
        # Weight tying: project with the embedding matrix transpose.
        return x @ self.wte.weight.T

    def loss(self, idx: mx.array, targets: mx.array, ignore_index: int = -100) -> mx.array:
        """Mean token NLL, skipping positions where target == ignore_index (pads)."""
        logits = self(idx)
        flat_logits = logits.reshape(-1, logits.shape[-1])
        flat_targets = targets.reshape(-1)
        per_token = nn.losses.cross_entropy(flat_logits, flat_targets, reduction="none")
        mask = flat_targets != ignore_index
        # Avoid div-by-zero on an all-pad batch (shouldn't happen in practice).
        return (per_token * mask).sum() / mx.maximum(mask.sum(), mx.array(1))

    def soft_loss(self, idx: mx.array, last_idx: mx.array, target_probs: mx.array) -> mx.array:
        """Soft CE at the last real context position against a full next-token mix.

        idx: (B, T) token ids
        last_idx: (B,) index of final context token in each row
        target_probs: (B, vocab) distribution (sums to 1 on supervised actions)
        """
        logits = self(idx)  # (B, T, V)
        b = logits.shape[0]
        # Gather logits at last context position: (B, V)
        gathered = logits[mx.arange(b), last_idx, :]
        log_probs = gathered - mx.logsumexp(gathered, axis=-1, keepdims=True)
        return -(target_probs * log_probs).sum(axis=-1).mean()
