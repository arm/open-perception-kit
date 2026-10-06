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

logging_internal_include = include_directories('internal')

logging = static_library(
  'logging',
  sources: [
    'internal/ConsoleOutputs.cpp',
    'internal/FileOutput.cpp',
    'internal/Log.cpp',
    'internal/Targets.cpp',
    'internal/tools.cpp',
    'internal/Logger.cpp',
  ],
  include_directories: [common_include, logging_internal_include],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  dependencies: [fmt_dep, threads_dep],
  pic: true,
  install: false,
)
