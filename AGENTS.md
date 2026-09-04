# TurboFire agent guide

This is the canonical guide for agents working on TurboFire's poker solver and
NashGPT policy-distillation pipeline. `CLAUDE.md` imports this file; update this
file rather than maintaining duplicate instructions.

## Mission and scope

TurboFire solves an abstract heads-up postflop poker game with Discounted CFR
(DCFR), records the solver's average strategies, and distills those strategies
into a small GPT.

The model input is:

```text
<START> <HOLE> As Ks <FLOP> Qs Jd 7c <BETTING> [history...]
```

The target is the complete probability distribution over legal actions, not a
sampled action or generated hand history:

```json
{"context":"<START> <HOLE> As Ks <FLOP> Qs Jd 7c <BETTING>","action_probs":{"HERO_CHECK":0.22,"HERO_BET100":0.78}}
```

Important terminology:

- **Solver label** means TurboFire's saved DCFR average strategy.
- **Model mix** means the neural network's masked and renormalized softmax.
- **GTO** should be used only when solver correctness and convergence support
  that claim. Model-to-label agreement alone proves distillation fidelity, not
  game-theoretic correctness.

## Invariants

- Tokenize only `context`, using whitespace.
- `action_probs` are floating-point labels, never vocabulary tokens.
- Every row is egocentric:
  - `<HOLE>` is the current actor's hand.
  - Targets are `HERO_*`.
  - `OPP_CHECK` means the other player checked.
- Solver seats remain `P1 = OOP/BB` and `P2 = IP/BTN`. Do not put seat names in
  model tokens.
- Train with soft cross-entropy at the final context position.
- At inference, mask to the legal `HERO_*` action set and renormalize at
  temperature 1.
- Canonicalize live hole cards and flop into the training suit frame unless
  explicitly testing raw-suit behavior.
- Do not combine datasets with incompatible ranges, action abstractions, token
  naming, solver semantics, or tree depths.

## Version history

### Legacy hard-label experiments

The earliest path emitted sampled trajectories into `trainingdata.txt` and
trained next-token prediction on the sampled action. This loses the solver's
mixed strategy: a 60/40 node becomes one hard action per trajectory.

Do not use this path to train a policy intended to reproduce GTO-like mixes.
It remains useful only as historical context or for testing generic sequence
generation.

### NashGPT 1.0: random physical flops

Archive: `src/old_training/nashgpt1.0/`

1.0 introduced soft-label training but treated suit-realized boards as ordinary
inputs:

- random physical flops (`CANONICAL=0`);
- approximately 96 sampled in-range combos per node;
- `SOFT_MAX_DEPTH=1`;
- 50 DCFR iterations per flop by default;
- egocentric `HERO_*` / `OPP_*` action tokens;
- raw card tokens at inference, without a required suit rewrite;
- separate wide/tight wrappers;
- random-row validation in the archived trainer;
- no versioned legal-action masking.

The historical dense dump reached roughly 31 million rows / 5 GB while covering
only about 238 distinct flops. This demonstrated that row count is a poor proxy
for poker-texture coverage.

What 1.0 improved:

- preserved soft solver action probabilities;
- established the JSONL-to-soft-CE pipeline;
- made large datasets trainable with streamed memmaps in PyTorch.

What needed improvement before 1.5:

- prioritize unique flop textures over duplicate rows;
- remove random suit redundancy;
- include every retained combo instead of a small combo sample;
- canonicalize at both data generation and inference.

Do not start new training from the archived 1.0 code.

### NashGPT 1.5: complete canonical flop coverage

Generator: `src/batch_training_soft.sh`

1.5 changed the data axis from random physical boards to complete suit-canonical
coverage:

- all 1,755 canonical flop textures;
- wide ranges by default, representing a single-raised-pot-like game;
- every retained live combo (`SOFT_MAX_COMBOS=0`);
- depth 1: OOP open plus the first IP response;
- 1,000 DCFR iterations per flop for production;
- approximately 11.5 million rows / 2.1 GB;
- runtime canonicalization in `src/llm/flop_iso.py`;
- the default action abstraction:
  - unopened: `CHECK`, `BET10`, `BET25`, `BET52`, `BET100`, `BET123`;
  - facing a bet: `FOLD`, `CALL`, `RAISE_2X`, `RAISE_3X`, `RAISE_4X`;
  - after the raise cap: `FOLD`, `CALL`.

The retained 1.5 checkpoint is an 8-layer, 8-head, 768-embedding PyTorch GPT
(about 57M parameters, vocabulary 85). Its metadata records:

- step 37,500, epoch 2;
- validation loss 0.372647;
- 11,242,987 training rows and 229,448 random-row validation rows
  (11,472,435 total).

Measured 1.5 distillation results:

- on a 12-texture canonical-suit evaluation, mean model-to-solver TVD was 1.67%
  across the 24 informative OOP-open/IP-after-check spots;
- canonical top-action agreement was 95.8%;
- bypassing canonicalization increased mean TVD to 10.95%;
- the checkpoint in `~/Projects/NashGPT/NashGPT-1.5-Iso` is byte-identical to
  `src/llm/out_torch_best/weights.pt`.

Those numbers show that the network learned the saved labels and that
canonicalization is required. They are not valid evidence that 1.5 learned GTO.
The pre-1.6 solver returned degenerate facing-bet behavior because terminal
utility was represented/indexed in a way that did not preserve each player's
own-hand utility through recursion.

What needed improvement before 1.6:

- fix player-relative counterfactual utility propagation and terminal indexing;
- track total investment across streets, not only current-street commitment;
- add known-answer solver tests before regenerating labels;
- hold out entire flop textures rather than random rows;
- use realistic condensed preflop ranges;
- support a compact, configurable bet tree with all-ins;
- train beyond the first response so the model can follow actual betting lines.

### NashGPT 1.6: corrected solver and deeper condensed game

Generator: `src/batch_training_soft_nashgpt16.sh`

1.6 is the current production data/model version:

- all 1,755 canonical flops;
- condensed ranges: BTN approximately 25%, BB approximately 28%;
- every retained live combo;
- `SOFT_MAX_DEPTH=3`, which emits decisions through a four-action line such as
  check -> bet -> raise -> call;
- unopened actions: `CHECK`, `BET40`, `BET100`, `ALLIN`;
- facing a bet: `FOLD`, `CALL`, `RAISE_3X`, `ALLIN`;
- facing a raise or all-in: `FOLD`, `CALL`;
- one-raise cap;
- 8,746,920 rows in the current dump (approximately 8.75M / 1.4 GB by the
  generator's capacity estimate; about 1.2 GB on the current filesystem).

The solver changes behind 1.6 include:

- separate P1 and P2 utility vectors indexed by each player's own combo;
- player-relative utility propagation through action and chance nodes;
- blocker-aware integration over the opponent's reach;
- cumulative `p1_invested` / `p2_invested` values that survive street changes;
- configurable bet fractions, raise multipliers, raise cap, and explicit all-in;
- duplicate-size suppression when an abstract size collapses to the same chip
  amount as another action;
- action-node/state consistency checks;
- configurable legal-action profiles shared by generation, training, and
  inference.

The first 1.6 pilot is an 8-layer, 8-head, 512-embedding PyTorch GPT (about
25.2M parameters, vocabulary 89), trained with a 155-whole-flop holdout:

- 7,974,400 training rows;
- 772,520 validation rows;
- best checkpoint at step 46,500, epoch 3;
- best held-out-flop validation loss 0.351801;
- later step 53,500 had validation loss 0.354844, so keeping `best` and stopping
  the plateaued run was correct.

A five-sample held-out-flop smoke comparison initially produced:

- same top action as the solver label in 5/5 spots;
- median TVD 1.90%;
- mean TVD 6.91%;
- worst TVD 23.92% on 72s at T86 two-tone, where the model underpredicted the
  label's all-in frequency.

The complete deterministic holdout benchmark was subsequently run over all
772,520 decisions from the 155 unseen canonical flops. Against the matching
saved DCFR average-strategy labels, production-canonicalized inference produced:

- mean TVD 6.24%, median 1.20%, p90 17.92%, p95 34.08%, and p99 80.40%;
- 92.95% top-action agreement;
- mean soft cross-entropy 0.35165;
- facing-bet nodes were weakest, with 11.17% mean TVD and 82.55% top-action
  agreement.

A strategically equivalent cyclic suit relabeling passed directly to the model
without canonicalization produced 24.17% mean TVD and 75.52% top-action
agreement. This confirms that production inference must canonicalize cards; the
network did not independently learn full invariance to arbitrary physical suit
labels.

The evaluator is `src/llm/eval_holdout.py`, and its detailed output is
`src/llm/nashgpt16_holdout_report.json`. These results measure distillation
fidelity to the saved solver labels, not exploitability or distance from true
GTO.

#### 1.6 learning finding

The 25M pilot provides credible evidence of board-state generalization. Its
validation set contains 155 entirely unseen canonical flop textures, rather
than random rows from flops present in training. Validation reached its minimum
around epoch 3 and then flattened/slightly worsened during epoch 4 while
training loss remained lower. The complete holdout benchmark confirms that the
model extracted useful relationships among hole cards, flop textures, and
betting paths instead of only memorizing exact rows. The low median error is
encouraging, but the high p95/p99 tail and weaker facing-bet decisions remain
material limitations.

Use the following wording when summarizing the result:

> The 25M NashGPT 1.6 model learned useful strategic structure that generalized
> to unseen canonical flop textures, reaching diminishing validation returns
> after roughly three epochs. It reproduced many held-out solver mixes closely,
> although performance on difficult mixed nodes remained uneven.

Do not interpret the plateau as proof that 25M parameters are the bottleneck.
It may also reflect label noise or solver convergence, optimization and
learning-rate choices, regularization, the condensed range, or the action
abstraction. The current evidence does not establish that 25M is fully
sufficient, nor that the learned policy is close to true GTO. Answer those
questions with solver-convergence/exploitability checks and a controlled
25M-versus-57M comparison using the same split, seed, labels, and training
recipe.

What should improve after 1.6 is the 1.7 methodology below. The implementation
is in `src/llm/`, but no production 1.7 checkpoint has been benchmarked yet.
Do not extend it by adding English reasoning, cloning a deeper flop tree, or
dumping every turn/river.

### NashGPT 1.7: mixed-node fidelity and suit-invariant inputs

1.7 is the current training/representation revision. It is not a new solver game.
Keep the 1.6 legal profile, condensed ranges, `SOFT_MAX_DEPTH=3`, and the
1,755-flop dump unless a convergence check proves the labels themselves are
unstable. The target remains a masked, renormalized mix over legal `HERO_*`
actions. Do not put English, chain-of-thought, or sampled hard actions on the
policy path.

1.6 already covers the one-raise-cap flop tree. History length is not the
failure mode. Depth-1 facing-bet nodes were weakest (11.17% mean TVD, 82.55%
top-action); depth-3 fold/call nodes were strongest. Soft CE already rewards
the dominant action, which is why top-action agreement is high while the mix
tail is not. 1.7 spends capacity on that tail and on making suit a relative
feature instead of a memorized letter.

#### Teacher checks before changing the student

Reweighting amplifies whatever the label is. Before upsampling mixed nodes:

1. Inspect the high-TVD tail from `src/llm/nashgpt16_holdout_report.json`,
   especially facing-bet and mixed check/all-in spots.
2. Rerun a handful of those flops at multiple DCFR iteration counts and seeds.
   If the teacher moves by a large TVD on the same node, regenerate or refuse
   to upweight that node; do not treat the gap as a model bug.
3. Add a small-game or best-response/exploitability check. DCFR
   self-consistency is not GTO.

Use the same 155-flop holdout (`--split flops --holdout-flops 155 --seed 42`)
for every 1.7 ablation. Version solver semantics, legal profile, ranges,
generation seed, source revision, and holdout list in every dataset/checkpoint
manifest.

#### Mixed-node training

Train sampling is currently uniform over JSONL rows. Facing-cap fold/call
nodes are the majority, so most steps train the easy binary mix.

Reweight **training only**:

- boost `facing_bet`;
- further boost high-entropy labels, e.g. `w = a_kind * (1 + H(q))`;
- leave validation uniform so `val_loss` stays comparable to 1.6.

Implement this as `WeightedRandomSampler` or a per-example loss scale in
`__getitem__` / collate. Do not duplicate hard rows in the JSONL.

An optional Brier or TVD term on the legal simplex is allowed as an auxiliary
loss. Soft CE can keep the top action correct while the mix stays sloppy;
Brier punishes that directly. Do not use temperature to hide a bad mix.

Run a controlled capacity ablation on the same labels and split: 25M
(`n_embd=512`) vs 57M (`n_embd=768`), plus a second 25M seed. If the
facing-bet tail barely moves, size is not the 1.7 bottleneck.

#### Suit representation

1.6 card tokens are fused (`As` is one vocab atom). `As` and `Ah` are
unrelated `wte` rows. Training shows only the 1,755 canonical colorings, so
the net learns canonical letter identities, not a suit-invariant texture
function. Texture is not stored in `wte`; it is computed in the 8 attention
and MLP blocks from several card positions. Canonicalization
(`src/llm/flop_iso.py`) is still the exact production path until a 1.7
raw-prompt holdout says otherwise.

1.7 representation:

- Split rank and suit into separate tokens:
  `<HOLE> A s K s <FLOP> Q s J d 7 c <BETTING>`.
- Vocab shrinks (drop 52 fused cards, add 13 ranks + 4 suits). Context grows
  by one token per card (+5 on a flop). `block_size=32` is enough. Parameter
  count is a wash; the point is shared rank embeddings, not a smaller net.
- Update `flop_split.py` and any parser that assumes three fused cards after
  `<FLOP>`.
- `Tokenizer.encode()` converts the existing fused 1.6 JSONL contexts to the
  split representation, so the solver dump does not need regeneration.

On-the-fly suit permutation during **training**, not a second dataset:

- When a train row is drawn, sample a permutation of `{s,h,d,c}` (or identity
  with some probability, e.g. 50/50) and rewrite only suit tokens.
- Keep the same `action_probs`. Mixes are suit-invariant; do not relabel
  `HERO_*`.
- This is a transform, not a copy. The epoch still has the same number of
  rows and steps. Do not materialize 24 JSONL copies. Do not look up a
  sibling flop; the 1.6 dump stores only canonical boards.
- Permute train rows only. Never backprop on holdout flops.

Do not add a validation "guidance loop" that injects failed colorings until
the net looks invariant. Validation has no gradient. Use permuted/raw
holdout TVD as an early-stop metric, the same way 1.6 uses `val_loss`.

Inference:

- Keep `flop_iso.py` until the 155-flop raw-prompt mean/median/p95 TVD
  approaches the canonical numbers.
- Rank-sort hole and flop if a run tries to skip the rewrite; otherwise card
  order is a second overfitting channel.
- Dropping the rewrite is allowed only after that raw holdout is documented
  in checkpoint metadata. Approximate invariance from SGD is not exact.

Relative suit IDs or pairwise same-suit bits (rank-ordered hole then flop,
then which of those cards share a suit) remain a valid alternative if prompts
need not look like `Ah Kh`. That encoding is invariant by construction and
does not need train-time permutation. Do not combine absolute `s/h/d/c`
tokens with pairwise bits in the same run; the net can ignore the bits and
memorize letters again.

#### Out of scope for 1.7 production

- English or chain-of-thought on the policy path. A commentary head for a UI
  is fine; it must not sit in front of `action_probs`.
- Growing flop betting depth past the 1.6 raise cap.
- Full turn/river JSONL over all textures. After facing-bet error and teacher
  stability improve, a sampled, reach-aware turn-only pilot is the next street
  experiment. Later streets should add a value head and nested re-solving,
  not clone every public node into the GPT.
- Suit augmentation as a substitute for unique flop textures. That is the 1.0
  failure mode.

1.7 success is measured against the 1.6 labels and the same 155 flops:
facing-bet mean TVD and p95/p99 TVD, plus canonical vs raw-prompt TVD if the
suit representation is supposed to remove `flop_iso.py`. Do not call the
result GTO.

## Comparative summary

| Version | Boards | Combos | Depth | Ranges | Main change | Trust level |
|---|---:|---:|---:|---|---|---|
| Legacy | sampled | sampled trajectories | variable | historical | hard next-token actions | not suitable for mixes |
| 1.0 | random physical | 96/node | 1 | wide/tight | first soft labels | archived; poor texture coverage |
| 1.5 | 1,755 canonical | all retained | 1 | wide | complete texture coverage + suit rewrite | good label imitation; old solver caveat |
| 1.6 | 1,755 canonical | all retained | 3 | condensed | corrected utilities + deeper configurable tree | useful held-out generalization; high-error tail and solver correctness still need validation |
| 1.7 | 1.6 labels unless teacher fails | all retained | 3 | condensed | reweight mixed nodes; split rank/suit; train-time suit permutation | implemented; unbenchmarked |

Do not compare validation loss across versions as if it were a leaderboard.
The datasets, legal action spaces, ranges, depths, model sizes, and validation
splits differ. Compare models against the matching solver/profile and use the
same deterministic holdout when running an ablation.

## Common test methodology

### 1. Build and deterministic solver tests

From `src/`:

```bash
make test
```

This must pass:

- C suit-isomorphism tests;
- DCFR terminal showdown/fold utility and blocker tests;
- player-swap/zero-sum checks;
- cumulative-investment transition checks;
- a known one-decision winner-calls / loser-folds game;
- configurable bet/raise/all-in legality tests;
- Python canonical-flop self-test over all 1,755 textures.

Run these before generating any dataset. A model can faithfully learn a solver
bug, so low validation loss does not replace solver tests.

### 2. Data-generation smoke

For 1.6:

```bash
cd src
make
SEED=42 ITERATIONS=50 TARGET_FLOPS=5 \
  OUT=training_soft_nashgpt16_smoke.jsonl \
  ./batch_training_soft_nashgpt16.sh
```

Check:

- JSON parses and every target sums to approximately 1;
- only legal actions for `nashgpt16` have nonzero mass;
- contexts contain no dead/duplicate cards;
- all targets are egocentric `HERO_*`;
- observed maximum history matches the requested depth;
- `.seen_flops` has the expected unique count;
- rerunning with the same output resumes instead of duplicating flops.

`ITERATIONS=50` is only a pipeline smoke test. Production labels use 1,000.

### 3. Solver convergence and semantics

For representative dry, paired, connected, two-tone, and monotone boards:

- compare average strategies at increasing iteration counts;
- verify strategically obvious known hands and actions;
- permute suits and require equivalent strategies after inverse mapping;
- swap players in controlled terminal games and require swapped/negated values;
- inspect mixed nodes, not only pure-action examples.

Prefer an independent reference implementation or a tractable exact game when
claiming GTO correctness.

### 4. Train/validation split

Use `--split flops`, not random rows, for new runs. Hold out full canonical
textures so validation measures generalization across boards:

```bash
--split flops --holdout-flops 155 --seed 42
```

The holdout list is written to the output directory and copied into checkpoint
metadata. Never compare runs that silently use different splits.

### 5. Model-to-solver evaluation

For each held-out decision:

- mask both distributions to the same legal profile;
- compare the full probability vectors;
- report soft cross-entropy and total variation distance:
  `TVD = 0.5 * sum(abs(model_prob - solver_prob))`;
- optionally report Brier score and Jensen-Shannon divergence in bits, matching
  `src/llm/eval_suit_generalization.py`;
- report top-action agreement only as a secondary metric;
- break results down by OOP/IP, history depth, action kind, hand class, and flop
  texture;
- show p50/p90/p95/max and the worst examples, not only a mean.

Always use canonicalized inference for production until a 1.7 run documents
that raw-prompt holdout TVD has caught up. A separate `--raw-prompt` run is
the invariance metric, not normal usage. Reweight training only; keep the
holdout unweighted.

### 6. Checkpoint selection

`torch_train.py` saves:

- `out/.../best`: lowest observed validation loss;
- `out/.../last`: latest evaluated state.

Deploy and download `best` unless a documented downstream metric selects a
different checkpoint. A flat/rising validation curve while training loss falls
is a reason to stop; the saved best checkpoint is already safe.

## Local inference

PyTorch's sampler always prints probabilities; it does not accept `--probs`:

```bash
cd src/llm
python3 torch_sample.py \
  --ckpt out_torch_16/best \
  --actions-only \
  --prompt "<START> <HOLE> Ah Kh <FLOP> Qh Jd 7c <BETTING>"
```

MLX's `sample.py` does require `--probs`:

```bash
python3 sample.py \
  --ckpt out_soft/best \
  --probs --actions-only \
  --prompt "<START> <HOLE> Ah Kh <FLOP> Qh Jd 7c <BETTING>"
```

## RunPod workflow

Use a PyTorch/CUDA image with a persistent `/workspace` volume. For the 1.6
pilot, a 4090-class GPU, at least 32 GB system RAM, and at least 40 GB disk is a
safe target. The JSONL is about 1.4 GB, but memmap construction temporarily
needs several times that size. MLX is macOS-only; do not install it on the pod.

### Prepare locally

Before paying for a GPU:

```bash
cd src
make test

cd llm
python3 torch_train.py --help
python3 torch_sample.py --help
```

Ensure these local artifacts exist:

- `src/llm/`;
- `src/training_soft_nashgpt16_condensed.jsonl`;
- enough local disk for the downloaded checkpoint.

### Connect

RunPod commonly exposes either a direct TCP endpoint:

```bash
ssh root@HOST -p PORT -i ~/.ssh/id_ed25519runpod
```

or its SSH proxy:

```bash
ssh POD_ID@ssh.runpod.io -i ~/.ssh/id_ed25519runpod
```

Host, port, and pod ID are ephemeral. Do not commit them or assume an old pod is
still reachable.

### Upload

Using direct TCP SSH (`scp` uses uppercase `-P`):

```bash
scp -P PORT -i ~/.ssh/id_ed25519runpod -r \
  src/llm root@HOST:/workspace/

scp -P PORT -i ~/.ssh/id_ed25519runpod \
  src/training_soft_nashgpt16_condensed.jsonl \
  root@HOST:/workspace/
```

With the proxy, omit `-P`:

```bash
scp -i ~/.ssh/id_ed25519runpod -r \
  src/llm POD_ID@ssh.runpod.io:/workspace/
```

For repeated transfers, prefer `rsync --partial --progress -e "ssh ..."` so an
interruption can resume.

### Install and verify on the pod

```bash
cd /workspace/llm
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements-torch.txt
python - <<'PY'
import torch
print(torch.__version__)
print(torch.cuda.is_available())
print(torch.cuda.get_device_name(0) if torch.cuda.is_available() else "NO CUDA")
PY
```

Do not start an expensive run if CUDA is unavailable.

### Train 1.6

Use `tmux` so an SSH disconnect does not terminate training:

```bash
tmux new -s nashgpt16
cd /workspace/llm
source .venv/bin/activate

python -u torch_train.py \
  --data ../training_soft_nashgpt16_condensed.jsonl \
  --cache cache_soft_16 \
  --out out_torch_16 \
  --legal-profile nashgpt16 \
  --n-layer 8 --n-head 8 --n-embd 512 \
  --batch-size 512 --epochs 5 \
  --split flops --holdout-flops 155 --seed 42 \
  2>&1 | tee train_nashgpt16.log
```

Detach with `Ctrl-b d`; reconnect with:

```bash
tmux attach -t nashgpt16
```

The first run streams JSONL into `cache_soft_16` memmaps. Reusing a compatible
cache avoids rebuilding it. Use a new cache directory or `--rebuild-cache` if
the dataset, block size, vocabulary, or legal profile changed. Do not assume a
cache is valid merely because the files exist.

### Monitor and stop

Watch validation loss, not individual 50-step training-loss noise. Evaluation
is slow because it scans the full holdout set. The long interval immediately
after each `val_loss` line is expected.

It is safe to interrupt after an evaluation because `last` is saved every
evaluation and `best` is saved whenever validation improves. Before stopping,
confirm that `out_torch_16/best/` contains:

- `weights.pt`;
- `config.json`;
- `tokenizer.json`;
- `meta.json`.

The trainer does not resume optimizer/model state from a checkpoint. Restarting
the command starts a new training run (though it can reuse the memmap cache).
Treat pod preemption between evaluations as lost training progress.

### Download artifacts

Download the entire `best` directory, not only `weights.pt`; inference requires
the matching config and tokenizer:

```bash
mkdir -p src/llm/out_torch_16

scp -P PORT -i ~/.ssh/id_ed25519runpod -r \
  root@HOST:/workspace/llm/out_torch_16/best \
  src/llm/out_torch_16/

scp -P PORT -i ~/.ssh/id_ed25519runpod \
  root@HOST:/workspace/llm/out_torch_16/holdout_flops.txt \
  src/llm/out_torch_16/
```

Proxy form:

```bash
scp -i ~/.ssh/id_ed25519runpod -r \
  POD_ID@ssh.runpod.io:/workspace/llm/out_torch_16/best \
  src/llm/out_torch_16/
```

Verify locally with `torch_sample.py`, preserve the training log if the run is
important, and stop/delete the pod after confirming the download. Persistent
volumes may continue to incur storage charges.

## Key files

| Path | Purpose |
|---|---|
| `src/dcfr.c`, `src/dcfr.h` | DCFR recursion, utilities, legal actions |
| `src/tree.c`, `src/tree.h` | Public game-tree construction and state |
| `src/ranges.c`, `src/ranges.h` | Wide, tight, and condensed ranges |
| `src/isomorphism.c` | Solver-side canonical flop/suit handling |
| `src/walk_tree.c` | Emit soft JSONL from `strategy_sum` |
| `src/batch_training_soft.sh` | Base canonical soft-label generator |
| `src/batch_training_soft_nashgpt16.sh` | Current 1.6 production profile |
| `src/tests/dcfr_test.c` | Solver correctness regression tests |
| `src/tests/isomorphism_test.c` | C suit-isomorphism tests |
| `src/llm/card_tokens.py` | Versioned fused/split card representation |
| `src/llm/flop_iso.py` | Inference canonicalization/inverse mapping |
| `src/llm/flop_split.py` | Whole-texture train/validation splitting |
| `src/llm/legal.py` | Versioned legal-action masks |
| `src/llm/nashgpt17.py` | Train-only suit transforms and sample weights |
| `src/llm/torch_train.py` | CUDA trainer with memmap cache |
| `src/llm/torch_sample.py` | PyTorch action-mix inference |
| `src/llm/train.py`, `src/llm/sample.py` | Apple MLX trainer/sampler |
| `src/old_training/nashgpt1.0/` | Archived 1.0 implementation |

## Do not

- Do not call model output GTO solely because it matches the training labels.
- Do not train on hard sampled lines when the target is an action mix.
- Do not grow datasets by duplicating physical suits instead of adding textures.
- Do not mix 1.5 and 1.6 rows; their ranges, depth, and action spaces differ.
- Do not mix 1.6 fused-card rows with 1.7 split rank/suit rows in one cache.
- Do not mix old seat-prefixed tokens with egocentric tokens.
- Do not evaluate held-out quality on random rows from already-seen flops.
- Do not print full-vocabulary softmax and treat the action mass as normalized.
- Do not use `--raw-prompt` in production inference unless a documented 1.7
  raw-holdout result has caught up to canonical TVD.
- Do not double the JSONL to learn suit permutations; rewrite suits on the
  fly during training.
- Do not backprop on holdout flops to "teach" invariance.
- Do not train English/CoT as the policy for 1.7.
- Do not download only model weights without config, tokenizer, and metadata.
- Do not keep a RunPod running after artifacts have been verified locally.
