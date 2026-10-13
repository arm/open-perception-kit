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
# This requires an available KMS display; see docs/arch/dmabuf-camera-probe.md.
# Keep all frames so the probe exposes corruption and gaps in the stream.
exec gst-launch-1.0 -e libcamerasrc camera-name="$camera" \
    ! video/x-raw,format=NV12,width=640,height=480,framerate=30/1 \
    ! identity name=frames silent=false eos-after=5401 \
    ! kmssink driver-name=vc4 connector-id="$connector" force-modesetting=true \
        sync=false enable-last-sample=false
