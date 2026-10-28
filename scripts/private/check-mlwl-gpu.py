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

"""Check the prepared RPi5 GPU runtime before starting an MLWL pipeline."""

import ctypes
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


def check_gpu():
    if not os.access("/dev/dri/renderD128", os.R_OK | os.W_OK):
        raise RuntimeError("/dev/dri/renderD128 is not readable and writable by this user")
    ctypes.CDLL("libmlworkloadlib.so")
    with tempfile.TemporaryDirectory(prefix="opk-gpu-") as directory:
        profile = Path(directory) / "vulkan.json"
        summary = subprocess.run(
            ["vulkaninfo", "--summary"], check=True, capture_output=True, text=True, timeout=30
        ).stdout
        devices = re.findall(r"^\s*deviceName\s*=\s*(.+)$", summary, re.MULTILINE)
        if len(devices) != 1 or not devices[0].startswith("V3D 7.1"):
            raise RuntimeError(
                f"expected only the RPi5 V3D GPU, found {devices}; check the image's Vulkan driver selection")
        subprocess.run(
            ["vulkaninfo", "--json", "-o", str(profile)],
            check=True, capture_output=True, text=True, timeout=30,
        )
        device = json.loads(profile.read_text())["capabilities"]["device"]
        # Match the pinned MLWL runtime's requiredWorkloadDeviceExtensions().
        missing = {
            "VK_ARM_data_graph",
            "VK_ARM_tensors",
            "VK_KHR_deferred_host_operations",
            "VK_KHR_maintenance5",
        } - device["extensions"].keys()
        if missing:
            raise RuntimeError(
                f"missing {', '.join(sorted(missing))}; supply a compatible MLWL GPU runtime")
    print(f"MLWL GPU preflight OK: {devices[0]} (required Vulkan extensions available)")


if __name__ == "__main__":
    runtime = os.environ.get("OPK_MLWL_RUNTIME", "")
    if runtime:
        try:
            if runtime != "rpi5-gpu":
                raise RuntimeError(f"unknown OPK_MLWL_RUNTIME: {runtime}")
            check_gpu()
        except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
            print(f"MLWL GPU preflight NOK: {error}", file=sys.stderr)
            if isinstance(error, subprocess.CalledProcessError):
                print(error.stderr.strip(), file=sys.stderr)
            sys.exit(1)
