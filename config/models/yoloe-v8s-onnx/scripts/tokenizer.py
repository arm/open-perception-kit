# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-FileCopyrightText: Copyright 2021 Gabriel Ilharco
# SPDX-License-Identifier: MIT
# Derived from OpenCLIP 2.30.0 SimpleTokenizer; see LICENSE.open_clip.
"""Faithful CLIP byte BPE without Torch; reject overflow instead of truncating queries."""

import gzip
import hashlib
import html
from collections import OrderedDict

import ftfy
import regex

VOCABULARY_SHA256 = "924691ac288e54409236115652ad4aa250f48203de50a9e4722a6ecd48d6804a"
CONTEXT_LENGTH = 77


def bytes_to_unicode():
    values = list(range(ord("!"), ord("~") + 1))
    values += list(range(ord("¡"), ord("¬") + 1))
    values += list(range(ord("®"), ord("ÿ") + 1))
    characters = values[:]
    extra = 0
    for value in range(256):
        if value not in values:
            values.append(value)
            characters.append(256 + extra)
            extra += 1
    return dict(zip(values, map(chr, characters)))


class Tokenizer:
    def __init__(self, vocabulary_path):
        with open(vocabulary_path, "rb") as stream:
            packed = stream.read()
        if hashlib.sha256(packed).hexdigest() != VOCABULARY_SHA256:
            raise ValueError("Tokenizer vocabulary digest differs from the qualified checkpoint")
        self.byte_encoder = bytes_to_unicode()
        merges = gzip.decompress(packed).decode("utf-8").split("\n")[1:49152 - 256 - 2 + 1]
        merges = [tuple(merge.split()) for merge in merges]
        vocabulary = list(self.byte_encoder.values())
        vocabulary += [value + "</w>" for value in self.byte_encoder.values()]
        vocabulary += ["".join(merge) for merge in merges]
        vocabulary += ["<start_of_text>", "<end_of_text>"]
        self.encoder = dict(zip(vocabulary, range(len(vocabulary))))
        self.ranks = dict(zip(merges, range(len(merges))))
        self.pattern = regex.compile(
            r"<start_of_text>|<end_of_text>|'s|'t|'re|'ve|'m|'ll|'d|[\p{L}]+|[\p{N}]|[^\s\p{L}\p{N}]+",
            regex.IGNORECASE,
        )
        # Bound cache by the admitted query bytes, rather than retaining every past word.
        self.cache = OrderedDict()

    def bpe(self, token):
        if token in self.cache:
            self.cache.move_to_end(token)
            return self.cache[token]
        encoded = self._bpe(token)
        self.cache[token] = encoded
        if len(self.cache) > 1024:
            self.cache.popitem(last=False)
        return encoded

    def _bpe(self, token):
        if token in ("<start_of_text>", "<end_of_text>"):
            return token
        word = tuple(token[:-1]) + (token[-1] + "</w>",)
        while len(word) > 1:
            pairs = set(zip(word, word[1:]))
            first, second = min(pairs, key=lambda pair: self.ranks.get(pair, float("inf")))
            if (first, second) not in self.ranks:
                break
            merged = []
            index = 0
            while index < len(word):
                try:
                    next_index = word.index(first, index)
                except ValueError:
                    merged.extend(word[index:])
                    break
                merged.extend(word[index:next_index])
                index = next_index
                if index + 1 < len(word) and word[index] == first and word[index + 1] == second:
                    merged.append(first + second)
                    index += 2
                else:
                    merged.append(word[index])
                    index += 1
            word = tuple(merged)
        return " ".join(word)

    def __call__(self, descriptions):
        rows = []
        for description in descriptions:
            # Match OpenCLIP's default lower cleaning, including ftfy and double HTML unescape.
            cleaned = html.unescape(html.unescape(ftfy.fix_text(description))).strip()
            cleaned = " ".join(cleaned.split()).strip().lower()
            if not cleaned or len(cleaned.encode("utf-8")) > 4096:
                raise ValueError("Normalized category is empty or exceeds4096 UTF-8 bytes")
            tokens = [49406]
            for token in self.pattern.findall(cleaned):
                encoded = "".join(self.byte_encoder[value] for value in token.encode("utf-8"))
                tokens.extend(self.encoder[part] for part in self.bpe(encoded).split(" "))
            tokens.append(49407)
            if len(tokens) > CONTEXT_LENGTH:
                raise ValueError("Category description exceeds75 content tokens; query was not truncated")
            rows.append(tokens + [0] * (CONTEXT_LENGTH - len(tokens)))
        return rows


def create_tokenizer(vocabulary_path):
    return Tokenizer(vocabulary_path)
