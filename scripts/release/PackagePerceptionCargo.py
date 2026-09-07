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
import tarfile
import tempfile
import zipfile
from pathlib import Path, PurePosixPath

CRATES_IO_INDEX = "sparse+https://index.crates.io/"


def package_sdk(sdk: Path, version: str, output_dir: Path, workspace: Path) -> None:
    workspace.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="perception-cargo-package-", dir=workspace
    ) as temporary:
        export_root = Path(temporary) / f"perception-sdk-{version}"
        with zipfile.ZipFile(sdk) as archive:
            archive.extractall(temporary)

        rust = export_root / "rust"
        manifest_path = rust / "Cargo.toml"
        manifest = manifest_path.read_text(encoding="utf-8")
        manifest, replacements = re.subn(
            r'(?m)^flatbuffers = "(=[^"]+)"$',
            r'flatbuffers = { version = "\1", registry = "crates-io" }',
            manifest,
        )
        if replacements != 1:
            raise RuntimeError("expected one pinned FlatBuffers dependency")
        manifest_path.write_text(manifest, encoding="utf-8")

        config_path = rust / ".cargo" / "config.toml"
        config = config_path.read_text(encoding="utf-8")
        config_path.write_text(
            config
            + "\n[registries.crates-io]\n"
            + f'index = "{CRATES_IO_INDEX}"\n',
            encoding="utf-8",
        )

        shutil.rmtree(rust / "crates")
        subprocess.run(
            ["cargo", "package", "--offline", "--locked"],
            cwd=rust,
            check=True,
        )

        crate = rust / "target" / "package" / f"perception-{version}.crate"
        root = f"perception-{version}"
        with tarfile.open(crate, "r:gz") as archive:  # NOSONAR
            for member in archive.getmembers():
                relative = PurePosixPath(member.name).relative_to(root)
                if relative.parts and relative.parts[0] in {
                    ".cargo",
                    "crates",
                    "vendor",
                }:
                    raise RuntimeError(
                        f"published crate contains {relative.parts[0]}"
                    )
            packaged_manifest = archive.extractfile(f"{root}/Cargo.toml")
            if packaged_manifest is None:
                raise RuntimeError("published crate has no Cargo.toml")
            cargo_toml = packaged_manifest.read().decode("utf-8")
        if f'registry-index = "{CRATES_IO_INDEX}"' not in cargo_toml:
            raise RuntimeError(
                "published FlatBuffers dependency is not pinned to crates.io"
            )

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
