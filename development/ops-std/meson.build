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

stdops = shared_module(
  'opk-std-ops',
  sources: [
    'Plugin.cpp',
    'GenericPostprocessOp.cpp',
    'GenericImagePreprocessOp.cpp',
    'InferenceControllerOp.cpp',

    'postproc/RvmParser.cpp',
    'postproc/DummyParser.cpp',
    'postproc/YoloParser.cpp',
    'postproc/YoloXParser.cpp',
    'postproc/UltrafaceParser.cpp',
    'postproc/PaddleocrParser.cpp',
    'postproc/GazeDetectionParser.cpp',
    'postproc/CameraContactParser.cpp',
    'postproc/PersonClassificationParser.cpp',
    'postproc/ImageNetClassificationParser.cpp',
    'postproc/ObjectEmbeddingParser.cpp',
    'postproc/ModNetSegmentationParser.cpp',
    'postproc/ScrfdParser.cpp',

    'preproc/GenericImageTensorBuilder.cpp',
    'preproc/CpuImageKernels.cpp',
    'preproc/CpuImageKernelsI420.cpp',
    'preproc/CpuImageKernelsNv12.cpp',
    'preproc/CpuImageKernelsYuy2.cpp'

  ],
  include_directories: common_include,
  dependencies: opk_deps ,
  link_with: [ common ],
  name_prefix: '',
  gnu_symbol_visibility: 'default',
  install: release_package,
  install_dir: 'lib/opk',
  install_rpath: '$ORIGIN',
)
