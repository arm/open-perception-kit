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

device="${1:?Pass the Anker capture node, for example /dev/video4}"
# Expected WSL failure and software control: docs/arch/dmabuf-camera-probe.md.
export GALLIUM_DRIVER="${GALLIUM_DRIVER:-d3d12}"
export GST_GL_WINDOW=wayland GST_GL_PLATFORM=egl GST_GL_API=gles2

# Requiring external-oes prevents successful fallback to a raw pixel upload.
exec gst-launch-1.0 -v v4l2src device="$device" io-mode=dmabuf num-buffers=60 \
    ! video/x-raw,format=YUY2,width=640,height=360,framerate=30/1 \
    ! identity drop-buffer-flags=corrupted \
    ! queue max-size-buffers=2 max-size-bytes=0 max-size-time=0 leaky=downstream \
    ! glupload \
    ! 'video/x-raw(memory:GLMemory),texture-target=external-oes' \
    ! glcolorconvert \
    ! glimagesinkelement sync=false enable-last-sample=false
