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

device="${1:?Pass the Integrated Camera capture node, for example /dev/video0}"
# WSL USB setup and measured limits: docs/arch/dmabuf-camera-probe.md.
# shortcut: damaged frames are dropped; remove the filter when USB/IP is reliable.
exec gst-launch-1.0 -v v4l2src device="$device" io-mode=dmabuf \
    ! video/x-raw,format=YUY2,width=640,height=480,framerate=30/1 \
    ! identity drop-buffer-flags=corrupted \
    ! queue max-size-buffers=2 max-size-bytes=0 max-size-time=0 leaky=downstream \
    ! glimagesink sync=false enable-last-sample=false
