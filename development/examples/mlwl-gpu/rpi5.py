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

"""Run UltraFace on RPi5 in the OPK Docker development container."""

import fcntl
import hashlib
import os
from pathlib import Path
import platform
import re
import subprocess
import sys

from prepare import REPO, prepare, run, runtime_fingerprint

MODEL = REPO / "config/models/ultraface-rfb-320-mlwl"
MODEL_SHA256 = "665e3f0ef67ea93a0b21c8acf3d33604ef4c3f82bd61860887beb35f5adba471"


def check_gpu(summary):
    def fields(name): return re.findall(rf"^\s*{name}\s*=\s*(.+)$", summary, re.MULTILINE)
    if (fields("vendorID") != ["0x14e4"] or
            fields("deviceType") != ["PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU"] or
            not re.fullmatch(r"V3D 7\.1.*", " ".join(fields("deviceName")))):
        raise RuntimeError("Expected one Raspberry Pi 5 V3D GPU; refusing CPU inference")


def main():
    if not Path("/.dockerenv").exists() or platform.machine() != "aarch64":
        raise RuntimeError("Run inside the AArch64 OPK Docker development container on Raspberry Pi 5")
    render = Path("/dev/dri/renderD128")
    if not render.exists() or Path("/sys/class/drm/renderD128/device/driver").resolve().name != "v3d":
        raise RuntimeError("Pass the RPi5 /dev/dri/renderD128 device into the container")
    if not os.access(render, os.R_OK | os.W_OK):
        if os.getgid() == render.stat().st_gid:
            raise RuntimeError(f"Cannot access {render}")
        # The container's render group ID may differ from the host's.
        os.execvp("sudo", ["sudo", "-n", "setpriv", f"--reuid={os.getuid()}",
                           f"--regid={render.stat().st_gid}", "--init-groups", "--reset-env",
                           "env", f"PATH={os.environ['PATH']}", sys.executable, __file__])
    model = MODEL / "ultraface-rfb-320.vgf"
    if not model.is_file():
        raise RuntimeError(f"Copy the OPK UltraFace VGF to {model}; see {MODEL / 'index.md'}")
    if hashlib.sha256(model.read_bytes()).hexdigest() != MODEL_SHA256:
        raise RuntimeError(f"Unexpected VGF checksum: {model}")

    cache_root = REPO / ".cache/mlwl-rpi5"
    cache_root.mkdir(parents=True, exist_ok=True)
    os.environ["CCACHE_DIR"] = str(cache_root / "ccache")
    with (cache_root / "lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        mlwl_lock = (REPO / "requirements/mlwl.json").read_bytes()
        sdk = cache_root / ("mlwl-" + hashlib.sha256(mlwl_lock).hexdigest()[:12])
        cache = cache_root / runtime_fingerprint(mlwl_lock)
        cache.mkdir(exist_ok=True)
        prepare(cache, sdk)
        os.environ["MLWorkloadLibraryForVulkan_DIR"] = str(sdk / "lib64/cmake/MLWorkloadLibraryForVulkan")
        build = cache_root / "opk-build"
        run("meson", "setup", build, REPO / "development",
            *(["--reconfigure"] if (build / "build.ninja").exists() else []),
            "--buildtype=release", "--layout=flat", "-Dtests=false", "-Dmlwl=enabled",
            "-Dexecutorch=disabled", "-Dpython_ops=disabled", f"-Dcmake_prefix_path={sdk}")
        run("meson", "compile", "-C", build, "-j2", "opchain-exec", "opk-std-ops", "opk-mlwl-ops")
        graph = cache / "emulation-build/graph"
        tensor = cache / "emulation-build/tensor"
        env = {key: value for key, value in os.environ.items() if not key.startswith("VK_")}
        env.update(VK_DRIVER_FILES=str(cache / "mesa-build/src/broadcom/vulkan/broadcom_devenv_icd.aarch64.json"),
                   VK_LAYER_PATH=f"{graph}:{tensor}", OPK_PROJECT_ROOT=str(REPO),
                   LD_LIBRARY_PATH=f"{sdk}/lib64:{build}/meson-out:{graph}:{tensor}:" + env.get("LD_LIBRARY_PATH", ""))
        summary = run("vulkaninfo", "--summary", env=env, stdout=subprocess.PIPE, text=True)
        print(summary.stdout, flush=True)
        check_gpu(summary.stdout)
        env["VK_INSTANCE_LAYERS"] = "VK_LAYER_ML_Graph_Emulation:VK_LAYER_ML_Tensor_Emulation"
        run(build / "meson-out/opchain-exec", MODEL / "opchain.json",
            REPO / "data/images/GettyImages-1140581459-thumbnail.jpg", env=env)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
