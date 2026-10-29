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

"""Run inside a prepared RPi5 GPU development container."""

import os
import subprocess
import sys
from pathlib import Path


check = Path(__file__).resolve().parents[2] / "scripts/private/check-mlwl-gpu.py"
env = {**os.environ, "OPK_MLWL_RUNTIME": "rpi5-gpu"}
subprocess.run([sys.executable, str(check)], env=env, check=True)

for override, message in (
    ({"VK_DRIVER_FILES": "/missing-vulkan-driver.json"}, "vulkaninfo"),
    ({"VK_INSTANCE_LAYERS": ""}, "VK_ARM_data_graph"),
):
    result = subprocess.run(
        [sys.executable, str(check)], env={**env, **override}, capture_output=True, text=True
    )
    if result.returncode == 0 or "preflight NOK" not in result.stderr or message not in result.stderr:
        raise RuntimeError(f"Invalid runtime passed or lacked diagnostics: {override}: {result.stderr}")
    print(f"Rejected invalid runtime: {override}")
