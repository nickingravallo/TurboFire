# NashGPT soft-label training

Train a small GPT on TurboFire DCFR average strategies. The 1.5 dump is
**every suit-canonical flop** (1,755 textures) × **every in-range combo** ×
`SOFT_MAX_DEPTH=1`, wide range. Live hands are rewritten into that frame at
inference and card tokens are mapped back afterward.

NashGPT 1.0 (random flops, 96-combo sample) is in
`src/old_training/nashgpt1.0/`.

## NashGPT 1.7

1.7 reuses the 1.6 condensed labels but changes the student representation and
training distribution:

- fused source cards such as `As` are encoded as rank/suit tokens `A s`;
- each training row has a 50% chance of a random non-identity global suit
  permutation; validation rows are never augmented;
- facing-bet and high-entropy labels receive higher sampling probability;
- cache metadata validates the dataset, block size, legal profile, and token
  format before reuse.

The source JSONL remains the canonical 1.6 dump; splitting and suit augmentation
happen while building/reading the training cache, so no 24-copy dataset is
created. The PyTorch defaults select the 1.6 data/profile and the 1.7
representation:

```bash
cd src/llm
python3 torch_train.py \
  --data ../training_soft_nashgpt16_condensed.jsonl \
  --cache cache_soft_17 --out out_torch_17 \
  --n-layer 8 --n-head 8 --n-embd 512 \
  --batch-size 512 --epochs 5 \
  --split flops --holdout-flops 155 --seed 42
```

Canonicalization remains enabled at inference until a complete raw-prompt
holdout benchmark demonstrates comparable mean, median, and tail TVD.
`--raw-prompt` is diagnostic.

## NashGPT 1.6

NashGPT 1.6 keeps all **1,755 suit-canonical flops**, uses condensed private
ranges (BTN ~25%, BB ~28%), and expands flop betting to:

- unopened: `CHECK`, `BET40`, `BET100`, `ALLIN`
- facing a bet: `FOLD`, `CALL`, `RAISE_3X`, `ALLIN`
- facing a raise or all-in: `FOLD`, `CALL`
- maximum line: four actions, e.g. check → bet → raise → call

Generate the production dump:

```bash
cd src
make
SEED=42 ./batch_training_soft_nashgpt16.sh
```

The script defaults to 1,000 DCFR iterations, all retained combos, and all
canonical flops. It resumes through the output `.seen_flops` file. Expected
size is approximately **8.75M rows / 1.4 GB JSONL**.

Run a five-flop smoke first if desired:

```bash
SEED=42 ITERATIONS=50 TARGET_FLOPS=5 \
  OUT=training_soft_nashgpt16_smoke.jsonl \
  ./batch_training_soft_nashgpt16.sh
```

Train the 25M-parameter pilot with a whole-flop holdout:

```bash
cd llm
python torch_train.py \
  --data ../training_soft_nashgpt16_condensed.jsonl \
  --cache cache_soft_16 --out out_torch_16 \
  --legal-profile nashgpt16 \
  --n-layer 8 --n-head 8 --n-embd 512 \
  --batch-size 512 --epochs 5 \
  --split flops --holdout-flops 155
```

Use `--legal-profile nashgpt16` so `BET40`, `ALLIN`, and the one-raise cap are
masked correctly during training. The profile is saved in checkpoint metadata
and loaded automatically by `torch_sample.py` / `sample.py`.

## NashGPT 1.5

## Setup

```bash
cd src/llm
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Requires macOS on Apple Silicon (MLX uses the Metal GPU).

## Generate data

From `src/`:

```bash
make
SEED=42 ./batch_training_soft.sh
```

That walks all 1,755 canonical flops, wide range, every hole combo the range
kept (~11.5M rows, ~2.1 GB). Default is **1000** DCFR iterations.
`SOFT_MAX_COMBOS=0` means no subsample. `ITERATIONS=50` is a smoke run.

`RANGE=tight` still works (3-bet-ish ranges) without a second script.

Each line:

```json
{"context":"<START> <HOLE> As Ks <FLOP> Qs Jd 7c <BETTING>","action_probs":{"HERO_CHECK":0.22,"HERO_BET100":0.25}}
```

## Train

```bash
cd src/llm
source .venv/bin/activate
python train.py --data ../training_soft_nashgpt15_wide.jsonl --out out_soft
```

Smoke:

```bash
python train.py --data ../training_soft_nashgpt15_wide.jsonl --out out_soft_smoke --epochs 1 --max-batches 50
```

## Inference (canonical in, original suits out)

```bash
python sample.py --ckpt out_soft/best --probs --actions-only \
  --prompt "<START> <HOLE> Ah Kh <FLOP> Qh Jd 7c <BETTING>"
```

`flop_iso.py` maps hole+flop onto a canonical texture, the net runs, then
card tokens in sampled lines are deconverted with the inverse suit perm.
`--probs` mixes (`HERO_*`) are not relabeled. `--raw-prompt` skips the rewrite.
`--keep-canonical` leaves generated cards in the training frame.

Standalone convert / deconvert:

```bash
python flop_iso.py --roundtrip "<START> <HOLE> Ah Kh <FLOP> Qh Jd 7c <BETTING>"
python flop_iso.py --canonicalize "<START> <HOLE> Ah Kh <FLOP> Qh Jd 7c <BETTING>"
python flop_iso.py --decanonicalize --perm 1,0,2,3 "<START> <HOLE> As Ks <FLOP> Qs Jd 7c <BETTING>"
```

## PyTorch / cloud GPU

```bash
pip install -r requirements-torch.txt
python torch_train.py --data ../training_soft_nashgpt15_wide.jsonl --out out_torch \
  --n-layer 8 --n-embd 768 --batch-size 256 --epochs 5
# default: --split flops --holdout-flops 155 (unseen textures in val)
# old random-row val: --split rows
python torch_sample.py --ckpt out_torch/best --actions-only \
  --prompt "<START> <HOLE> Ah Kh <FLOP> Qh Jd 7c <BETTING>"
```

## Layout

| File | Purpose |
|------|---------|
| `tokenizer.py` | Whitespace vocab (+ seeded cards/actions) |
| `card_tokens.py` | Fused/split card representation and shared parsing |
| `nashgpt17.py` | Train-only suit augmentation and mixed-node weights |
| `model.py` | Tiny causal GPT; hard + soft CE |
| `train.py` | MLX trainer (NashGPT 1.5 defaults) |
| `sample.py` | Mix readout; canonical in, deconvert cards out |
| `flop_iso.py` | Suit rewrite + inverse map (matches `src/isomorphism.c`) |
| `../batch_training_soft.sh` | 1,755 flops × all wide combos × depth 1 |
| `../old_training/nashgpt1.0/` | Archived random-flop 1.0 trainer |
