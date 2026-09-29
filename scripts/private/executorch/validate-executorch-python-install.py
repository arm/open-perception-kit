#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import importlib
from importlib import metadata


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate the installed ExecuTorch Python package."
    )
    parser.add_argument("version", help="Expected ExecuTorch version")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    importlib.import_module("executorch")
    importlib.import_module("torch")
    installed = metadata.version("executorch").split("+", 1)[0]
    if installed != args.version:
        raise SystemExit(
            f"installed ExecuTorch version {installed} does not match {args.version}"
        )


if __name__ == "__main__":
    main()
