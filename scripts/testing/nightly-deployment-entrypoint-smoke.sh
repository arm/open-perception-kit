#!/usr/bin/env bash
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

# Run the deployment image's real entrypoint; this image is built in archive jobs.
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "Usage: $0 <deployment-image> <models-directory> <logs-directory>" >&2
    exit 2
fi

image="$1"
models_dir="$(realpath "$2")"
mkdir -p "$3"
logs_dir="$(realpath "$3")"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

container_id="$(docker create \
    -e OPK_PIPELINE=/nightly/pipeline.json \
    -v "$script_dir/nightly-deployment-pipeline.json:/nightly/pipeline.json:ro" \
    -v "$models_dir/ultraface-rfb-320:/work/config/models/ultraface-rfb-320:ro" \
    -v "$models_dir/nitec-resnet-18-executorch:/work/config/models/nitec-resnet-18-executorch:ro" \
    "$image")"
trap 'docker rm -f "$container_id" >/dev/null' EXIT

status=0
timeout 120s docker start --attach "$container_id" 2>&1 | tee "$logs_dir/deployment-smoke.txt" || status=$?
python3 "$script_dir/../health-report/container-size.py" "$container_id" \
    --model-dir "$models_dir/ultraface-rfb-320" \
    --model-dir "$models_dir/nitec-resnet-18-executorch" > "$logs_dir/container-size.json"
if ! docker cp "$container_id:/tmp/nightly-deployment-cascade.ndjson" "$logs_dir/deployment-cascade.ndjson"; then
    if [[ $status -eq 0 ]]; then
        exit 1
    fi
fi
if [[ $status -ne 0 ]]; then
    exit "$status"
fi

cat "$logs_dir/deployment-cascade.ndjson" | tee -a "$logs_dir/deployment-smoke.txt"
docker run --rm --user "$(id -u):$(id -g)" --entrypoint /opt/opk-venvs/python-ops-runtime/bin/python \
    -v "$logs_dir/deployment-cascade.ndjson:/nightly/results.ndjson:ro" \
    -v "$script_dir/check-nightly-cascade.py:/nightly/check-nightly-cascade.py:ro" \
    "$image" /nightly/check-nightly-cascade.py /nightly/results.ndjson \
    2>&1 | tee -a "$logs_dir/deployment-smoke.txt"
