# TurboFire — LLM / soft-label training notes

Context for agents working on the poker solver → neural policy pipeline.

## Goal

Distill TurboFire DCFR average strategies into a small GPT that, given
`(hole, flop, betting path)`, predicts the **action mix** (softmax over legal
actions), not a single sampled hand history.

## Data format (soft labels)

Each JSONL line is one decision:

```json
{"context":"<START> <HOLE> As Ks <FLOP> Qs Jd 7c <BETTING>","action_probs":{"SB_CHECK":0.22,"SB_BET100":0.25}}
```

- **Tokenize only `context`** (whitespace split).
- **`action_probs` are float targets**, not tokens.
- Train with soft CE at the last context position against the full mix.
- At inference, print softmax (`--probs` / `torch_sample.py`), don’t only sample one token.

Hard-line `trainingdata.txt` (sampled trajectories) is legacy. Prefer soft JSONL.

## Vocab / tokens

- **Vocab size ≈ 84**: `<PAD>` `<UNK>` + specials + 52 cards + 22 `SB_`/`BB_` actions.
- Context length typically **9–13 tokens** (mean ~12).
- Soft probs are never in the vocab.

## Solver emit (diversity)

`walk_tree` emits soft JSONL. Defaults favor **many flops**, not dense trees:

| Env | Default | Meaning |
|-----|--------:|---------|
| `SOFT_MAX_DEPTH` | 1 | Emit SB open + BB reply only |
| `SOFT_MAX_COMBOS` | 96 | Sample this many combos per node |

Dense full-tree dumps (~100k+ rows/flop) cause low flop diversity. Sparse emit
is ~670 rows/flop.

Generate data:

```bash
cd src
make
TARGET_FLOPS=12000 SOFT_MAX_DEPTH=1 SOFT_MAX_COMBOS=96 ITERATIONS=50 \
  OUT=training_soft_diverse.jsonl ./batch_training_soft.sh
```

- Track unique flops via `${OUT}.seen_flops`.
- Prefer a **new** diverse file; don’t keep growing the old dense ~5GB / 238-flop dump.

## How much data

| Priority | Target |
|----------|--------|
| Unique flops | **10k–15k** (main lever) |
| Rows | **~7–10M** sparse |
| JSONL size | **~1.2–1.8 GB** |

More rows on the same flops barely helps. More distinct flops does.
~2–5M diverse rows is enough to train; ~8–10M is the “make it good” band.

Old dense file: ~31M rows / 5GB / only ~238 flops — volume without coverage.

## Model size (params)

Formula roughly matches `llm/model.py` / `llm/torch_model.py`
(vocab=84, block=32, biasless linears, tied lm head):

| Config | n_layer | n_embd | Params |
|--------|--------:|-------:|-------:|
| Current local | 6 | 384 | **~10.7M** |
| Good-small | 8 | 512 | **~25.2M** |
| **Recommended** | 8 | 768 | **~56.7M** |
| Good-large | 12 | 768 | **~85.1M** |

For this discrete task (short seq, 84 vocab, action mixes), **~57M is enough**.
Capacity is rarely the bottleneck vs flop diversity / label quality.

## Training

### Local (MLX, Apple Silicon)

```bash
cd src/llm
source .venv/bin/activate
python train.py --data ../training_soft_diverse.jsonl --out out_soft
python sample.py --ckpt out_soft/best --probs --actions-only \
  --prompt "<START> <HOLE> As Ks <FLOP> Qs Jd 7c <BETTING>"
```

16GB Air: do **not** load the full 31M dense file in RAM. Use ~4–8M rows or the
diverse ~1.5GB file. `train.py` currently materializes arrays in memory.

### Cloud (PyTorch / CUDA)

MLX won’t run on typical Linux GPUs. Use:

```bash
pip install -r requirements-torch.txt
python torch_train.py --data ../training_soft_diverse.jsonl --out out_torch \
  --n-layer 8 --n-embd 768 --batch-size 256 --epochs 5
python torch_sample.py --ckpt out_torch/best --actions-only \
  --prompt "<START> <HOLE> As Ks <FLOP> Qs Jd 7c <BETTING>"
```

`torch_train.py` streams JSONL → memmaps under `cache_soft/`.

## GPU budget (~$10)

At ~$0.40/hr (4090-class), **one good run is ~$1–3**, not $10.
Don’t inflate the dataset to spend money — use leftover budget for retries/seeds.

**Recommended “good” run:**

- Model: **8×768 (~57M)**
- Data: **12k flops → ~8M rows → ~1.4 GB** JSONL
- Epochs: **4–5**, batch **256–512**
- Pod: 4090/L4/A10, **≥32 GB system RAM**, ≥40 GB disk
- Hosts: RunPod / Vast.ai, PyTorch+CUDA template

Spend plan for ~$10: main run + 2nd seed + optional 85M ablation + buffer.

## Inference

- Softmax at \(T=1\) (no temperature for mix readout).
- `--actions-only` keeps the acting player’s `SB_`/`BB_` prefix and renormalizes.
- Softmax over full vocab won’t sum to 1 on actions alone — mask then renorm.

## Key files

| Path | Role |
|------|------|
| `src/walk_tree.c` | Soft JSONL emit from `strategy_sum` |
| `src/batch_training_soft.sh` | Diverse flop batch generator |
| `src/llm/train.py` | MLX soft/hard trainer |
| `src/llm/torch_train.py` | PyTorch soft trainer (cloud) |
| `src/llm/torch_model.py` | PyTorch GPT |
| `src/llm/sample.py` | MLX sample / `--probs` |
| `src/llm/torch_sample.py` | PyTorch mix readout |
| `src/llm/tokenizer.py` | Whitespace vocab + seeded cards/actions |

## Don’t

- Train for GTO mixes on hard sampled lines only.
- Chase 5–10GB of duplicate flops.
- Assume printing softmax alone calibrates mixes without soft-label training.
- Expect the LM to be a solver — it’s a distilled imitator; eval vs solver mixes.
