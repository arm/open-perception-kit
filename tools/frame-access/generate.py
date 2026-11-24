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

"""Generate the native outer contract, independently of FlowData metadata."""
import argparse
import json
from pathlib import Path
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--flatc", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    descriptor = json.loads((root / "tools/perception/sdk.json").read_text(encoding="utf-8"))
    required_version = descriptor["flatbuffers"]["version"]
    version = subprocess.check_output([args.flatc, "--version"], text=True).strip()
    if version != f"flatc version {required_version}":
        parser.error(f"OPK requires flatc {required_version}; found {version}")
    schema = root / "schemas/frame-access/frame_reply.fbs"
    args.output.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [args.flatc, "--cpp", "--rust", "--binary", "--schema",
         "-o", str(args.output.resolve()), str(schema)],
        check=True,
    )


if __name__ == "__main__":
    main()
