#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Configure Perception Cargo metadata for private-registry publication."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

CRATES_IO_INDEX = "sparse+https://index.crates.io/"


def set_flatbuffers_registry(manifest_path: Path) -> None:
    """Make the release-only crate resolve FlatBuffers from crates.io.

    This rewrite is unnecessary if Perception itself moves to crates.io.
    """
    manifest = manifest_path.read_text(encoding="utf-8")
    manifest, replacements = re.subn(
        r'(?m)^flatbuffers = "(=[^"]+)"$',
        r'flatbuffers = { version = "\1", registry = "crates-io" }',
        manifest,
    )
    if replacements != 1:
        raise RuntimeError("expected one pinned FlatBuffers dependency")
    manifest_path.write_text(manifest, encoding="utf-8")


def add_crates_io_registry(config_path: Path) -> None:
    """Define the registry name used while Cargo writes the crates.io index URL."""
    config = config_path.read_text(encoding="utf-8")
    config_path.write_text(
        config
        + "\n[registries.crates-io]\n"
        + f'index = "{CRATES_IO_INDEX}"\n',
        encoding="utf-8",
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--cargo-config", type=Path, required=True)
    args = parser.parse_args()
    set_flatbuffers_registry(args.manifest)
    add_crates_io_registry(args.cargo_config)


if __name__ == "__main__":
    main()
