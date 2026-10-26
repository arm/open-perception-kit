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

"""Build the private Vulkan runtime used by the Raspberry Pi example."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tools/perception"))
from artifacts import acquire_artifact  # noqa: E402
from sdk_config import LockedArtifact  # noqa: E402


def run(*args, **kwargs):
    print("+", *args, flush=True)
    return subprocess.run([str(arg) for arg in args], cwd=REPO, check=True, **kwargs)


def patch(path, before, after):
    text = path.read_text()
    if after in text:
        return
    if text.count(before) != 1:
        raise RuntimeError(f"Unexpected source in {path}")
    path.write_text(text.replace(before, after))


def runtime_fingerprint(mlwl_lock):
    return hashlib.sha256(Path(__file__).read_bytes() + mlwl_lock +
                          Path(__file__).with_name("sources.json").read_bytes()).hexdigest()[:12]


def prepare(cache, sdk):
    if (cache / "ready").is_file():
        print(f"Using cached GPU runtime: {cache}", flush=True)
        return
    sudo = [] if os.geteuid() == 0 else ["sudo", "-n"]
    run(*sudo, "apt-get", "update")
    run(*sudo, "apt-get", "install", "-y", "--no-install-recommends",
        "bison", "flex", "python3-mako", "python3-yaml", "glslang-tools",
        "libdrm-dev", "libvulkan1", "vulkan-tools", "zlib1g-dev", "libzstd-dev")
    if not sdk.exists():
        run(sys.executable, REPO / "scripts/private/install-mlwl.py", sdk)

    sources = {}
    lock = json.loads(Path(__file__).with_name("sources.json").read_text())
    for name, value in lock.items():
        artifact = LockedArtifact(**value)
        source = cache / f"{artifact.name}-{artifact.version}"
        if not source.exists():
            archive = acquire_artifact(artifact, cache.parent / "downloads")
            with tempfile.TemporaryDirectory(dir=cache) as temporary:
                with tarfile.open(archive) as tar:
                    tar.extractall(temporary, filter="data")
                (Path(temporary) / source.name).rename(source)
        sources[name] = source

    # shortcut: these driver changes are POC-tested; run Vulkan CTS before deployment.
    mesa = sources.pop("mesa")
    device = mesa / "src/broadcom/vulkan/v3dv_device.c"
    pipeline = mesa / "src/broadcom/vulkan/v3dv_pipeline.c"
    patch(device, ".KHR_device_group ", ".KHR_deferred_host_operations = true,\n      .KHR_device_group ")
    patch(device, ".shaderInt64 = false", ".shaderInt64 = true")
    patch(pipeline, ".lower_uadd_sat = true,",
          ".lower_int64_options = ~0,\n      .late_lower_int64 = true,\n      .lower_uadd_sat = true,")
    patch(pipeline, "NIR_PASS(_, nir, nir_lower_load_const_to_scalar);",
          "NIR_PASS(_, nir, nir_lower_int64);\n   NIR_PASS(_, nir, nir_lower_load_const_to_scalar);")
    mesa_build = cache / "mesa-build"
    run("meson", "setup", mesa_build, mesa,
        *(["--reconfigure"] if (mesa_build / "build.ninja").exists() else []),
        "--buildtype=release", "-Db_ndebug=false", "-Dgallium-drivers=",
        "-Dvulkan-drivers=broadcom", "-Dplatforms=", "-Dglx=disabled", "-Degl=disabled",
        "-Dgbm=disabled", "-Dgles1=disabled", "-Dgles2=disabled", "-Dopengl=false",
        "-Dllvm=disabled", "-Dvideo-codecs=", "-Dbuild-tests=false", "-Dvulkan-layers=",
        "-Dxmlconfig=disabled", env={**os.environ, "PYTHONPATH": "/usr/lib/python3/dist-packages"})
    run("ninja", "-C", mesa_build, "-j2", "src/broadcom/vulkan/libvulkan_broadcom.so",
        "src/broadcom/vulkan/broadcom_devenv_icd.aarch64.json",
        env={**os.environ, "PYTHONPATH": "/usr/lib/python3/dist-packages"})
    emulation = sources.pop("emulation")
    run("cmake", "-S", emulation, "-B", cache / "emulation-build", "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DVMEL_USE_FLOAT_AS_DOUBLE=ON",
        "-DVMEL_TESTS_ENABLE=OFF", "-DVMEL_BUILD_DOCS=OFF",
        f"-DVULKAN_HEADERS_PATH={sdk}",
        *(f"-D{name}_PATH={path}" for name, path in sources.items()))
    run("cmake", "--build", cache / "emulation-build", "--target", "VkLayer_Graph", "VkLayer_Tensor", "-j2")
    (cache / "ready").touch()
