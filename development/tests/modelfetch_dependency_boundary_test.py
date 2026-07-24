#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Verify that modelfetch stays confined and its test backend matches the runtime seam.

Model-loading and its tests compile the real header-only C++ API. The fake
replaces only the C ABI backend, so every production import must have exactly
one corresponding fake export and the fake must not retain extra symbols.
"""

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


def dynamic_symbols(readelf: Path, artifact: Path, *, undefined: bool) -> set[str]:
    completed = subprocess.run(
        [str(readelf), "--dyn-syms", "--wide", str(artifact)],
        check=True,
        capture_output=True,
        text=True,
    )
    symbols = set()
    for line in completed.stdout.splitlines():
        columns = line.split()
        if len(columns) < 8 or not columns[0].rstrip(":").isdigit():
            continue
        is_undefined = columns[6] == "UND"
        name = columns[7].split("@", 1)[0]
        if is_undefined == undefined and name.startswith("modelfetch_"):
            symbols.add(name)
    return symbols


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("readelf", type=Path)
    parser.add_argument("--non-model-consumer", type=Path, nargs="+", required=True)
    parser.add_argument("--model-runtime", type=Path, required=True)
    parser.add_argument("--model-consumer", type=Path, nargs="+", required=True)
    parser.add_argument("--fake-backend", type=Path, required=True)
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
    runtime_imports = dynamic_symbols(args.readelf, args.model_runtime, undefined=True)
    if not runtime_imports:
        raise AssertionError(f"{args.model_runtime.name} has no modelfetch runtime imports")
    fake_exports = dynamic_symbols(args.readelf, args.fake_backend, undefined=False)
    missing_fake_symbols = runtime_imports - fake_exports
    unused_fake_symbols = fake_exports - runtime_imports
    if missing_fake_symbols or unused_fake_symbols:
        raise AssertionError(
            "fake modelfetch backend differs from runtime imports: "
            f"missing={sorted(missing_fake_symbols)}, unused={sorted(unused_fake_symbols)}"
        )

    for artifact in args.model_consumer:
        dependencies = needed_libraries(args.readelf, artifact)
        if "libpek-model-loading.so" not in dependencies:
            raise AssertionError(
                f"{artifact.name} does not depend on libpek-model-loading.so"
            )


if __name__ == "__main__":
    main()
