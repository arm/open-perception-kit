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

set -euo pipefail

camera="${1:?Pass the camera-name reported by gst-device-monitor-1.0 Video}"
connector="${2:?Pass the DRM connector ID reported by kmsprint -p}"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
guard_dir="$(mktemp -d /tmp/opk-dmabuf-no-copy.XXXXXX)"
probe_pid=""

# shellcheck disable=SC2317 # Called by the EXIT trap.
cleanup() {
    if [[ -n "$probe_pid" ]]; then
        kill -TERM "$probe_pid" 2> /dev/null || true
        wait "$probe_pid" 2> /dev/null || true
    fi
    rm -rf -- "$guard_dir"
}
trap 'cleanup' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Neither source nor sink has a no-copy property; block their copy API calls.
compile_flags="$(pkg-config --cflags --libs gstreamer-video-1.0)"
# shellcheck disable=SC2086 # pkg-config returns separate compiler arguments.
cc -std=c11 -Wall -Wextra -Werror -fPIC -shared \
    "$script_dir/private/dmabuf-no-copy.c" $compile_flags -o "$guard_dir/no-copy.so"
preload="$guard_dir/no-copy.so${LD_PRELOAD:+:$LD_PRELOAD}"
guard_status=0
OPK_DMABUF_GUARD_CHECK=1 LD_PRELOAD="$preload" \
    gst-launch-1.0 --version > /dev/null || guard_status=$?
if [[ "$guard_status" != 77 ]]; then
    echo "DMA-BUF probe: copy guard did not load; refusing to start capture." >&2
    exit 1
fi

# This requires an available KMS display; see docs/arch/dmabuf-camera-probe.md.
# Keep all frames so the probe exposes corruption and gaps in the stream.
OPK_DMABUF_GUARD_CHECK=0 LD_PRELOAD="$preload" \
    gst-launch-1.0 -e libcamerasrc camera-name="$camera" \
    ! video/x-raw,format=NV12,width=640,height=480,framerate=30/1 \
    ! identity name=frames silent=false eos-after=5401 \
    ! kmssink driver-name=vc4 connector-id="$connector" force-modesetting=true \
        sync=false enable-last-sample=false &
probe_pid=$!
status=0
wait "$probe_pid" || status=$?
probe_pid=""
exit "$status"
