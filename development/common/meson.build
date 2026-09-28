# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

common = shared_library(
  'opk-common',
  sources: [

    'gst/Tools.cpp',
    'gst/TensorMeta.cpp',

    'opk/Result.cpp',
    'opk/Tools.cpp',
    'opk/Labels.cpp',
    'opk/Model.cpp',
    'opk/ModelDescriptor.cpp',
    'opk/PipelinePreset.cpp',

    'opk/AudioBuffer.cpp',

    'opk/Base64.cpp',

    'opk/FrameResults.cpp',

    'op/Op.cpp',
    'op/OpRef.cpp',
    'op/OpChain.cpp',
    'op/OpChainContext.cpp',
    'op/OpChainDescriptor.cpp',

    'perf/PerformanceMetrics.cpp',

    'raster/BitmapFont.cpp',
    'raster/SegmentationMask.cpp',
    'raster/SurfacePainter.cpp',

    'mediaio/Common.cpp',
    'mediaio/VideoFrame.cpp',
    'mediaio/GstVideoFrame.cpp',
    'mediaio/PixelBufferVideoFrame.cpp',
  ],
  include_directories: [ common_include ],
  dependencies: [ opk_deps, config_validator_dep ],
  link_whole: [logging],
  gnu_symbol_visibility: 'default',
  install: release_package,
  install_dir: 'lib/opk',
  install_rpath: '$ORIGIN',
)
