from __future__ import annotations

import argparse
import tempfile
import unittest
from pathlib import Path

import numpy as np
import torch
from torch.utils.data import Dataset

from card_tokens import (
    TOKEN_FORMAT_SPLIT,
    fuse_cards,
    section_cards,
    split_cards,
)
from flop_iso import canonicalize_runout
from flop_split import flop_card_ids, flop_strings, split_by_flop
from legal import KIND_FACE_BET, KIND_OPEN
from nashgpt17 import permute_batch_suits, sampling_weights, suit_id_lookups
from tokenizer import Tokenizer
from torch_train import (
    CACHE_VERSION,
    REPRESENTATION_VERSION,
    SuitPermutationDataset,
    cache_matches,
)


CONTEXTS = (
    "<START> <HOLE> As Ks <FLOP> Qs Jd 7c <BETTING>",
    "<START> <HOLE> Ah Kh <FLOP> Qh Js 7d <BETTING> OPP_CHECK",
    "<START> <HOLE> 2s 2h <FLOP> Ac Ad Kc <BETTING>",
)


def encoded_matrix(tokenizer: Tokenizer, contexts: tuple[str, ...]) -> np.ndarray:
    rows = [tokenizer.encode(context) for context in contexts]
    width = max(map(len, rows))
    x = np.full((len(rows), width), tokenizer.pad_id, dtype=np.int32)
    for index, row in enumerate(rows):
        x[index, : len(row)] = row
    return x


class NashGPT17Tests(unittest.TestCase):
    def setUp(self) -> None:
        records = [{"context": context, "action_probs": {"HERO_CHECK": 1.0}} for context in CONTEXTS]
        self.tokenizer = Tokenizer.build_from_soft(records, TOKEN_FORMAT_SPLIT)

    def test_split_vocab_and_encoding(self) -> None:
        self.assertNotIn("As", self.tokenizer.token_to_id)
        self.assertIn("A", self.tokenizer.token_to_id)
        self.assertIn("s", self.tokenizer.token_to_id)
        self.assertEqual(
            self.tokenizer.encode(CONTEXTS[0]),
            self.tokenizer.encode(split_cards(CONTEXTS[0])),
        )
        self.assertEqual(fuse_cards(split_cards(CONTEXTS[0])), CONTEXTS[0])

    def test_card_sections_accept_both_formats(self) -> None:
        expected = ["Qs", "Jd", "7c"]
        self.assertEqual(section_cards(CONTEXTS[0], "<FLOP>", 3), expected)
        self.assertEqual(
            section_cards(split_cards(CONTEXTS[0]), "<FLOP>", 3),
            expected,
        )

    def test_flop_split_reads_six_tokens(self) -> None:
        x = encoded_matrix(self.tokenizer, CONTEXTS)
        ids = flop_card_ids(x, self.tokenizer)
        self.assertEqual(ids.shape, (3, 6))
        self.assertEqual(
            flop_strings(ids, self.tokenizer),
            ["Qs Jd 7c", "Qh Js 7d", "Ac Ad Kc"],
        )
        train, val, info = split_by_flop(x, self.tokenizer, seed=42, holdout_flops=1)
        self.assertEqual(len(train) + len(val), 3)
        self.assertEqual(info["flops_total"], 3)

    def test_suit_permutation_changes_only_suits(self) -> None:
        x = encoded_matrix(self.tokenizer, (CONTEXTS[0],))
        lookups = suit_id_lookups(self.tokenizer)
        out = permute_batch_suits(
            x,
            lookups,
            np.random.default_rng(7),
            probability=1.0,
        )
        before = [self.tokenizer.id_to_token[int(i)] for i in x[0]]
        after = [self.tokenizer.id_to_token[int(i)] for i in out[0]]
        for left, right in zip(before, after):
            if left in "shdc":
                self.assertIn(right, "shdc")
            else:
                self.assertEqual(left, right)

    def test_dataset_augmentation_is_wrapper_scoped(self) -> None:
        suit_ids = torch.tensor(
            [self.tokenizer.token_to_id[suit] for suit in "shdc"],
            dtype=torch.int32,
        )

        class OneRow(Dataset):
            def __len__(self) -> int:
                return 1

            def __getitem__(self, _index: int):
                return suit_ids.clone(), torch.tensor(3), torch.ones(1), torch.ones(1)

        base = OneRow()
        augmented = SuitPermutationDataset(base, self.tokenizer, probability=1.0)
        train_ids = augmented[0][0]
        val_ids = base[0][0]
        self.assertFalse(torch.equal(train_ids, val_ids))
        self.assertTrue(torch.equal(val_ids, suit_ids))

    def test_mixed_node_sampling_weights(self) -> None:
        probs = np.asarray([[1.0, 0.0], [0.5, 0.5]], dtype=np.float32)
        kinds = np.asarray([KIND_OPEN, KIND_FACE_BET], dtype=np.uint8)
        weights = sampling_weights(probs, kinds, 2.0, 1.0)
        self.assertEqual(weights[0], 1.0)
        self.assertGreater(weights[1], 2.0)

    def test_canonicalizer_accepts_split_prompt(self) -> None:
        fused = canonicalize_runout(CONTEXTS[0]).canonical
        split = canonicalize_runout(split_cards(CONTEXTS[0])).canonical
        self.assertEqual(split, fused)

    def test_cache_validation_includes_representation_and_dataset(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = root / "data.jsonl"
            cache = root / "cache"
            cache.mkdir()
            (cache / "kind.npy").touch()
            data.write_text("{}\n")
            args = argparse.Namespace(
                data=data,
                cache=cache,
                block_size=32,
                legal_profile="nashgpt16",
                token_format=TOKEN_FORMAT_SPLIT,
            )
            stat = data.stat()
            meta = {
                "data": str(data.resolve()),
                "data_size": stat.st_size,
                "data_mtime_ns": stat.st_mtime_ns,
                "block_size": 32,
                "legal_profile": "nashgpt16",
                "token_format": TOKEN_FORMAT_SPLIT,
                "cache_version": CACHE_VERSION,
                "representation_version": REPRESENTATION_VERSION,
            }
            self.assertTrue(cache_matches(meta, args)[0])
            args.block_size = 31
            self.assertFalse(cache_matches(meta, args)[0])


if __name__ == "__main__":
    unittest.main()
