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
frames="${3:-5400}"
if [[ ! "$frames" =~ ^[1-9][0-9]*$ || ${#frames} -gt 10 ]] || ((frames > 2147483646)); then
    echo "Frame count must be between 1 and 2147483646." >&2
    exit 2
fi
stream_port="${4:-}"
if [[ -n "$stream_port" ]] && { [[ ! "$stream_port" =~ ^[1-9][0-9]*$ || ${#stream_port} -gt 5 ]] || ((stream_port > 65535)); }; then
    echo "TCP port must be between 1 and 65535." >&2
    exit 2
fi
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
guard_dir="$(mktemp -d /tmp/opk-dmabuf-no-copy.XXXXXX)"
probe_pid=""
sink_pid=""
reader_pid=""

# shellcheck disable=SC2317 # Called by the EXIT trap.
cleanup() {
    for pid in "$reader_pid" "$sink_pid" "$probe_pid"; do
        if [[ -n "$pid" ]]; then
            kill -TERM "$pid" 2> /dev/null || true
        fi
    done
    for pid in "$reader_pid" "$sink_pid" "$probe_pid"; do
        if [[ -n "$pid" ]]; then
            wait "$pid" 2> /dev/null || true
        fi
    done
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

if [[ -n "$stream_port" ]]; then
    compile_flags="$(pkg-config --cflags --libs libdrm)"
    # shellcheck disable=SC2086 # pkg-config returns separate compiler arguments.
    cc -std=c11 -Wall -Wextra -Werror "$script_dir/private/kms-snapshot.c" \
        $compile_flags -o "$guard_dir/capture"
fi

# Read back the connector used below; retries cover camera startup and shutdown.
stream_frames() {
    trap - EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    exec 3> "$guard_dir/frames"
    while true; do
        ready=false
        for ((attempt = 0; attempt < 100; ++attempt)); do
            if "$guard_dir/capture" "$connector" "$guard_dir/frame.nv12" 2> "$guard_dir/readback.log"; then
                ready=true
                break
            else
                result=$?
                if [[ "$result" != 3 ]]; then
                    cat "$guard_dir/readback.log" >&2
                    return "$result"
                fi
            fi
            sleep 0.1
        done
        if [[ "$ready" != true ]]; then
            cat "$guard_dir/readback.log" >&2
            echo "Timed out waiting for the camera framebuffer." >&2
            return 1
        fi
        cat "$guard_dir/readback.log" >&2
        cat "$guard_dir/frame.nv12" >&3
        rm "$guard_dir/frame.nv12"
        sleep 0.1
    done
}

# Camera/display path: the original DMA buffers go directly to KMS under the guard.
OPK_DMABUF_GUARD_CHECK=0 LD_PRELOAD="$preload" \
    gst-launch-1.0 -e libcamerasrc camera-name="$camera" \
    ! video/x-raw,format=NV12,width=640,height=480,framerate=30/1 \
    ! identity name=frames silent=false eos-after="$((frames + 1))" \
    ! kmssink driver-name=vc4 connector-id="$connector" force-modesetting=true \
        sync=false enable-last-sample=false &
probe_pid=$!
status=0
if [[ -n "$stream_port" ]]; then
    # Remote preview: copy only from the displayed framebuffer, then encode for TCP.
    mkfifo "$guard_dir/frames"
    GST_DEBUG=2 env -u GST_DEBUG_FILE gst-launch-1.0 -q fdsrc fd=0 \
        ! rawvideoparse format=nv12 width=640 height=480 framerate=10/1 colorimetry=bt709 \
        ! videoconvert ! jpegenc quality=85 ! multipartmux \
        ! tcpserversink host=127.0.0.1 port="$stream_port" sync=false buffers-max=20 \
        < "$guard_dir/frames" &
    sink_pid=$!
    stream_frames &
    reader_pid=$!
    echo "Camera → guarded kmssink (connector $connector); KMS readback → TCP 127.0.0.1:$stream_port" >&2
    finished=""
    wait -n -p finished "$probe_pid" "$sink_pid" "$reader_pid" || status=$?
    if [[ "$finished" != "$probe_pid" && "$status" == 0 ]]; then
        echo "Preview stopped before the camera probe finished." >&2
        status=1
    fi
    if [[ "$finished" == "$probe_pid" ]]; then
        probe_pid=""
    elif [[ "$finished" == "$sink_pid" ]]; then
        sink_pid=""
    elif [[ "$finished" == "$reader_pid" ]]; then
        reader_pid=""
    fi
else
    wait "$probe_pid" || status=$?
    probe_pid=""
fi
exit "$status"
