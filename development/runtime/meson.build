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

runtime_include = include_directories('..')

opk_runtime = shared_library(
  'opk-runtime',
  sources: [
    'Logging.cpp',
    'OpChain.cpp',
    'PerformanceMetrics.cpp',
    'Pipeline.cpp',
    'Result.cpp',
    'Tools.cpp',
    'VideoFrame.cpp',
  ],
  include_directories: [ runtime_include, common_include ],
  dependencies: opk_deps,
  link_with: [ common ],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  gnu_symbol_visibility: 'default',
  name_prefix: '',
  install: release_package,
  install_dir: 'lib/opk',
  install_rpath: '$ORIGIN',
)
