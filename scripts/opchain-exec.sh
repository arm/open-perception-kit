#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
runtime_dir="${OPK_ONNXRUNTIME_ROOT:-/opt/opk-deps/onnxruntime}/lib"
if [[ -z "${OPK_ONNXRUNTIME_ROOT:-}" && ! -r "$runtime_dir/libonnxruntime.so.1" ]]; then
    runtime_dir="$root/.cache/opchain-exec"
fi
export LD_LIBRARY_PATH="$root/development/build-active/meson-out:$runtime_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$root/tools/opchain-exec" "$@"
