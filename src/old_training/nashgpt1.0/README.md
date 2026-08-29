# NashGPT 1.0 (archived)

Snapshot of the first soft-label trainer: random physical flops, 96 combos
per node, `SOFT_MAX_DEPTH=1`. Replaced by NashGPT 1.5 (1,755 canonical
flops × every in-range combo + suit rewrite at inference).

Do not train new models from here. Live code is `src/llm/` and
`src/batch_training_soft.sh`.

## What this dump looked like

| Knob | 1.0 |
|------|-----|
| Boards | Random physical flops (`TARGET_FLOPS` ~10k, `CANONICAL=0`) |
| Combos | `SOFT_MAX_COMBOS=96` sample per node |
| Depth | `SOFT_MAX_DEPTH=1` |
| Infer | Raw hole+flop tokens (no required suit rewrite) |

Wrappers `batch_training_soft_wide.sh` / `_tight.sh` and the deprecated
`batch_training_data.sh` lived in `src/` and only sampled random boards.

To replay 1.0 data gen, copy the scripts back to `src/` or:

```bash
cd src
BINARY=./turbofire RANGE=wide CANONICAL=0 TARGET_FLOPS=10000 \
  SOFT_MAX_COMBOS=96 ./old_training/nashgpt1.0/batch_training_soft.sh
```
