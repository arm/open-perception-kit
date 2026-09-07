#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Package Perception Cargo artifacts from a verified SDK release ZIP."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import tempfile
import zipfile
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


def package_sdk(sdk: Path, version: str, output_dir: Path, workspace: Path) -> None:
    workspace.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="perception-cargo-package-", dir=workspace
    ) as temporary:
        export_root = Path(temporary) / f"perception-sdk-{version}"
        with zipfile.ZipFile(sdk) as archive:
            archive.extractall(temporary)

        rust = export_root / "rust"
        set_flatbuffers_registry(rust / "Cargo.toml")
        add_crates_io_registry(rust / ".cargo" / "config.toml")

        shutil.rmtree(rust / "crates")
        subprocess.run(
            ["cargo", "package", "--offline", "--locked"],
            cwd=rust,
            check=True,
        )

        crate = rust / "target" / "package" / f"perception-{version}.crate"
        output_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(crate, output_dir / crate.name)
        wheel = export_root / "python" / (
            f"opk_perception_sdk-{version}-py3-none-any.whl"
        )
        shutil.copy2(wheel, output_dir / wheel.name)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--workspace", type=Path, required=True)
    args = parser.parse_args()
    package_sdk(args.sdk, args.version, args.output_dir, args.workspace)


if __name__ == "__main__":
    main()
