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

"""Measure a Docker container and optionally its externally mounted model data."""

import argparse
import json
import subprocess


def inspect(*args):
    return json.loads(subprocess.check_output(["docker", *args], text=True))[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("container")
    parser.add_argument("--model-dir", action="append", default=[])
    args = parser.parse_args()

    container = inspect("container", "inspect", "--size", args.container)
    image = inspect("image", "inspect", container["Image"])
    model_bytes = 0
    for directory in args.model_dir:
        output = subprocess.check_output(["du", "-sb", directory], text=True)
        model_bytes += int(output.split()[0])

    result = {
        "image_bytes": image["Size"],
        "writable_layer_bytes": container["SizeRw"],
        "external_model_bytes": model_bytes,
        "total_bytes": image["Size"] + container["SizeRw"] + model_bytes,
    }
    print(json.dumps(result))


if __name__ == "__main__":
    main()
