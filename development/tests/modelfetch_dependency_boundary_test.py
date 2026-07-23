#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Verify that modelfetch stays confined to model-loading binaries."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


NEEDED_PATTERN = re.compile(r"\(NEEDED\).*Shared library: \[([^\]]+)\]")


def needed_libraries(readelf: Path, artifact: Path) -> set[str]:
    completed = subprocess.run(
        [str(readelf), "-d", str(artifact)],
        check=True,
        capture_output=True,
        text=True,
    )
    return set(NEEDED_PATTERN.findall(completed.stdout))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("readelf", type=Path)
    parser.add_argument("--non-model-consumer", type=Path, nargs="+", required=True)
    parser.add_argument("--model-runtime", type=Path, required=True)
    parser.add_argument("--model-consumer", type=Path, nargs="+", required=True)
    args = parser.parse_args()

    forbidden = {"libmodelfetch_c.so", "libpek-model-loading.so"}
    for artifact in args.non_model_consumer:
        unexpected = needed_libraries(args.readelf, artifact) & forbidden
        if unexpected:
            raise AssertionError(
                f"{artifact.name} unexpectedly depends on {sorted(unexpected)}"
            )

    runtime_dependencies = needed_libraries(args.readelf, args.model_runtime)
    if "libmodelfetch_c.so" not in runtime_dependencies:
        raise AssertionError(
            f"{args.model_runtime.name} does not depend on libmodelfetch_c.so"
        )

    for artifact in args.model_consumer:
        dependencies = needed_libraries(args.readelf, artifact)
        if "libpek-model-loading.so" not in dependencies:
            raise AssertionError(
                f"{artifact.name} does not depend on libpek-model-loading.so"
            )


if __name__ == "__main__":
    main()
